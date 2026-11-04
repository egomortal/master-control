/* SPDX-License-Identifier: GPL-3.0-or-later
 * Master Control (PadBridge/EeveeLution 0.3.16 by X-F1REBALL-X, on AnyPad-PS5
 * and ps5-wireless-pads). 0.4.0 adds USB drivers ported from Ghost-Control by
 * StonedModder and its Manba V2 NBJr fork: see gc_usb.h and CREDITS.md. */
#include "ps5_wired.h"
#include "host.h"
#include "log.h"
#include "util.h"
#include "xinput.h"
#include "profiles.h"
#include "ps5_vpad.h"
#include "gc_usb.h"

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define N_IN        4               /* reads kept queued per controller */
#define X_OUT       N_IN
#define N_X         (N_IN + 1)
#define PKT         64
#define OUTQ        8
#define T_SCAN      1000            /* ms between looks for new controllers */
#define MAX_SEEN    48

typedef struct {
    int used;
    int fd;
    int slot;                       /* virtual-pad slot */
    char path[24];
    xi_pad xp;
    pad_state st;
    pad_state web_state;         /* copied under g_info_lock after each input */
    int iface;
    uint8_t ep_in, ep_out;
    struct usb_fs_endpoint ep[N_X];
    void *bufp[N_X][1];
    uint32_t lenp[N_X][1];
    uint8_t buf[N_X][PKT];
    int busy[N_X];
    uint8_t outq[OUTQ][PKT];
    int outq_len[OUTQ], outq_head, outq_n;
    int init_next;                  /* next GIP set-up packet to queue */
    uint32_t sent;                  /* frames sent to the console */
    long t_open;
    int first_logged;
    /* 0.3.8: plain USB HID gamepads (GameSir and others in HID mode),
     * read through the same descriptor-driven parser as Bluetooth pads. */
    const pad_profile *prof;            /* NULL: GIP / XInput */
    uint8_t pctx[PROFILE_CTX_MAX];
    /* Master Control 0.4.0: drivers ported from Ghost-Control (gc_usb.c). */
    int gk;                             /* GCU_* */
    gcu_switch sw;                      /* Switch Pro USB handshake */
} wdev;

#define HID_MAP_DIR    "/data/mastercontrol/maps"
#ifndef USB_SHORT_XFER_OK
#define USB_SHORT_XFER_OK 0x0004
#endif
#define ALL_HID_FLAG   "/data/mastercontrol/all_hid"    /* take any HID gamepad, not only known non-Sony brands */

/* HID gamepads taken by default: brands that make Xbox / PC pads. Sony and
 * PlayStation-licensed pads are left to the console, which drives them. */
static int hid_vendor_ok(uint16_t vid)
{
    static const uint16_t ok[] = {
        0x3537, 0x2F24,             /* GameSir */
        0x1532,                     /* Razer */
        0x045E,                     /* Microsoft */
        0x2DC8,                     /* 8BitDo */
        0x057E,                     /* Nintendo (Switch Pro and pads emulating it) */
        0x0E6F, 0x24C6, 0x20D6,     /* PDP, PowerA */
        0x0079, 0x11C0, 0x2563, 0x1949, /* generic Chinese pads (DragonRise, Betop, ShanWan, ...) */
    };
    size_t i;

    if (vid == 0x054C) return 0;    /* Sony: never */
    if (access(ALL_HID_FLAG, F_OK) == 0) return 1;
    for (i = 0; i < sizeof ok / sizeof ok[0]; i++) if (ok[i] == vid) return 1;
    return 0;
}

/* The HID interfaces (alternate setting 0) with an interrupt IN endpoint,
 * in descriptor order. */
static int find_hid_ifaces(const uint8_t *c, int len, int *ifaces, uint8_t *ins, int max)
{
    int i = 0, n = 0, cur = -1;

    while (i + 2 <= len && n < max) {
        int bl = c[i], type = c[i + 1];
        if (bl < 2 || i + bl > len) break;
        if (type == 4 && bl >= 9) {
            cur = (c[i + 3] == 0 && c[i + 5] == 0x03) ? c[i + 2] : -1;
        } else if (type == 5 && bl >= 7 && cur >= 0) {
            if ((c[i + 2] & 0x80) && (c[i + 3] & 3) == 3) {
                ifaces[n] = cur;
                ins[n] = c[i + 2];
                n++;
                cur = -1;           /* one IN per interface */
            }
        }
        i += bl;
    }
    return n;
}

/* Master Control: the interrupt OUT endpoint of interface `want` (alternate
 * setting 0), or 0. Switch Pro pads need it for the USB handshake. */
static uint8_t find_out_ep(const uint8_t *c, int len, int want)
{
    int i = 0, cur = -1;

    while (i + 2 <= len) {
        int bl = c[i], type = c[i + 1];
        if (bl < 2 || i + bl > len) break;
        if (type == 4 && bl >= 9) cur = (c[i + 3] == 0) ? c[i + 2] : -1;
        else if (type == 5 && bl >= 7 && cur == want && !(c[i + 2] & 0x80) && (c[i + 3] & 3) == 3)
            return c[i + 2];
        i += bl;
    }
    return 0;
}

/* GET_DESCRIPTOR(HID report) for an interface. Returns its length or 0. */
static int hid_report_desc(int fd, int iface, uint8_t *buf, int max)
{
    struct usb_ctl_request req;

    memset(&req, 0, sizeof req);
    req.ucr_data = buf;
    req.ucr_flags = USB_SHORT_XFER_OK;
    req.ucr_request.bmRequestType = UT_READ_INTERFACE;
    req.ucr_request.bRequest = UR_GET_DESCRIPTOR;
    USETW(req.ucr_request.wValue, 0x2200);
    USETW(req.ucr_request.wIndex, iface);
    USETW(req.ucr_request.wLength, max);
    if (ioctl(fd, USB_DO_REQUEST, &req) != 0) return 0;
    return req.ucr_actlen > 0 && req.ucr_actlen <= max ? req.ucr_actlen : 0;
}

/* Paths looked at and found not to be ours, so that other devices are not
 * opened every second: remembered until the node changes or goes away. */
typedef struct { char path[24]; dev_t rdev; ino_t ino; } seen_t;

static pthread_t g_thr;
static volatile int g_run, g_pause, g_started;
static wired_events g_ev;
static char g_skip[32];
static wdev g_dev[WIRED_MAX_PADS];
static seen_t g_seen[MAX_SEEN];
static int g_nseen;
static int g_poll_ok = 1, g_spurious;
static pthread_mutex_t g_info_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- transfers ------------------------------------------------------------ */

static int x_start(wdev *d, int i, int len)
{
    struct usb_fs_start st;

    d->lenp[i][0] = (uint32_t)len;
    d->ep[i].nFrames = 1;
    d->ep[i].aFrames = 0;
    d->ep[i].status = 0;
    memset(&st, 0, sizeof st);
    st.ep_index = (uint8_t)i;
    if (ioctl(d->fd, USB_FS_START, &st) != 0) return 0;
    d->busy[i] = 1;
    return 1;
}

static void out_kick(wdev *d)
{
    if (d->busy[X_OUT] || !d->outq_n || !d->ep_out) return;
    memcpy(d->buf[X_OUT], d->outq[d->outq_head], (size_t)d->outq_len[d->outq_head]);
    if (x_start(d, X_OUT, d->outq_len[d->outq_head])) {
        d->outq_head = (d->outq_head + 1) % OUTQ;
        d->outq_n--;
    } else {
        log_line("wired %s: write failed (errno %d)", d->path, errno);
        d->outq_n = 0;
    }
}

static void out_push(wdev *d, const uint8_t *p, int n)
{
    int at;

    if (n <= 0 || n > PKT || d->outq_n >= OUTQ) return;
    at = (d->outq_head + d->outq_n) % OUTQ;
    memcpy(d->outq[at], p, (size_t)n);
    d->outq_len[at] = n;
    d->outq_n++;
    out_kick(d);
}

static void queue_gip_init(wdev *d)
{
    uint8_t p[PKT];
    int n, i;

    for (i = 0; (n = xi_gip_init_packet(&d->xp, i, p, sizeof p)) > 0; i++) out_push(d, p, n);
}

/* ---- finding controllers ----------------------------------------------------- */

/* The first interface (alternate setting 0) that speaks GIP or XInput, with
 * its interrupt IN and OUT endpoints. */
static int find_iface(const uint8_t *c, int len, int *iface, int *kind, uint8_t *in, uint8_t *out)
{
    int i = 0, cur = -1, cur_kind = XI_NONE;

    *in = *out = 0;
    while (i + 2 <= len) {
        int bl = c[i], type = c[i + 1];
        if (bl < 2 || i + bl > len) break;
        if (type == 4 && bl >= 9) {             /* interface */
            if (cur >= 0 && *in) break;         /* the previous one was complete */
            cur = -1;
            *in = *out = 0;
            if (c[i + 3] == 0) {
                cur_kind = xi_kind_of_interface(c[i + 5], c[i + 6], c[i + 7]);
                if (cur_kind != XI_NONE) cur = c[i + 2];
            }
        } else if (type == 5 && bl >= 7 && cur >= 0) {  /* endpoint */
            uint8_t addr = c[i + 2], attr = c[i + 3] & 3;
            if (attr == 3 || attr == 2) {
                if ((addr & 0x80) && !*in) *in = addr;
                if (!(addr & 0x80) && !*out) *out = addr;
            }
        }
        i += bl;
    }
    if (cur >= 0 && *in) {
        *iface = cur;
        *kind = cur_kind;
        return 1;
    }
    return 0;
}

static int seen_before(const char *path, const struct stat *sb)
{
    int i;

    for (i = 0; i < g_nseen; i++)
        if (strcmp(g_seen[i].path, path) == 0) {
            if (g_seen[i].rdev == sb->st_rdev && g_seen[i].ino == sb->st_ino) return 1;
            g_seen[i] = g_seen[--g_nseen];      /* a different device now */
            return 0;
        }
    return 0;
}

static void remember(const char *path, const struct stat *sb)
{
    if (g_nseen >= MAX_SEEN) g_nseen = 0;       /* forget all; costs one look each */
    snprintf(g_seen[g_nseen].path, sizeof g_seen[g_nseen].path, "%s", path);
    g_seen[g_nseen].rdev = sb->st_rdev;
    g_seen[g_nseen].ino = sb->st_ino;
    g_nseen++;
}

/* One log line per path and error, not one a second. */
static struct { char path[24]; int err; } g_errs[32];

static int note_error(const char *path, int err)
{
    int i, free_i = -1;

    for (i = 0; i < 32; i++) {
        if (g_errs[i].path[0] && strcmp(g_errs[i].path, path) == 0) {
            if (g_errs[i].err == err) return 0;
            g_errs[i].err = err;
            return 1;
        }
        if (!g_errs[i].path[0] && free_i < 0) free_i = i;
    }
    if (free_i < 0) free_i = 0;
    snprintf(g_errs[free_i].path, sizeof g_errs[free_i].path, "%s", path);
    g_errs[free_i].err = err;
    return 1;
}

static int in_use(const char *path)
{
    int i;

    for (i = 0; i < WIRED_MAX_PADS; i++)
        if (g_dev[i].used && strcmp(g_dev[i].path, path) == 0) return 1;
    return 0;
}

static void dev_close(wdev *d, const char *why)
{
    struct usb_fs_uninit un;
    int slot = d->slot;

    if (!d->used) return;
    log_line("wired %s: %s (%u frames sent)", d->path, why, (unsigned)d->sent);
    memset(&un, 0, sizeof un);
    ioctl(d->fd, USB_FS_UNINIT, &un);           /* stops and closes every transfer */
    close(d->fd);
    pthread_mutex_lock(&g_info_lock);
    d->used = 0;
    d->fd = -1;
    pthread_mutex_unlock(&g_info_lock);
    if (g_ev.on_disconnect) g_ev.on_disconnect(slot);
}

static int try_claim_hid(int fd, const char *path, const struct stat *sb, const struct usb_device_info *di,
                         int iface, uint8_t in, const uint8_t *rd, int rl, int gk, uint8_t out_ep);

static int try_claim(const char *path)
{
    struct stat sb;
    struct usb_device_info di;
    struct usb_gen_descriptor gd;
    struct usb_fs_init init;
    struct usb_fs_open op;
    uint8_t cfg[1024];
    int fd, iface = 0, kind = XI_NONE, i, n, k, gk;
    uint8_t in, out;
    wdev *d = NULL;

    if (stat(path, &sb) != 0) return 0;
    if (seen_before(path, &sb)) return 0;
    for (i = 0; i < WIRED_MAX_PADS; i++)
        if (!g_dev[i].used) { d = &g_dev[i]; break; }
    if (!d) return 0;                           /* full: look again later */

    fd = open(path, O_RDWR);
    if (fd < 0) {                               /* busy, or no rights yet: look again later */
        if (note_error(path, errno)) log_line("wired %s: cannot open (errno %d)", path, errno);
        return 0;
    }
    memset(&di, 0, sizeof di);
    memset(&gd, 0, sizeof gd);
    gd.ugd_data = cfg;
    gd.ugd_maxlen = sizeof cfg;
    gd.ugd_config_index = 0xFF;
    /* The device descriptor, as hci_usb.c reads the Bluetooth chip's: this
     * console answers USB_GET_DEVICEINFO with ENOTTY (its struct differs). */
    {
        struct usb_device_descriptor dd;
        memset(&dd, 0, sizeof dd);
        if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
            if (note_error(path, 1000 + errno))
                log_line("wired %s: no device descriptor (errno %d)", path, errno);
            close(fd);
            return 0;                           /* maybe still enumerating: look again */
        }
        if (ioctl(fd, USB_GET_DEVICEINFO, &di) != 0) memset(&di, 0, sizeof di);
        di.udi_vendorNo = UGETW(dd.idVendor);
        di.udi_productNo = UGETW(dd.idProduct);
        di.udi_releaseNo = UGETW(dd.bcdDevice);
        if (gcu_kind(di.udi_vendorNo, di.udi_productNo, 0) == GCU_IGNORE) {
            log_line("wired %s: %04x:%04x is a Manba receiver in idle/update mode: not a pad",
                     path, di.udi_vendorNo, di.udi_productNo);
            close(fd);
            remember(path, &sb);
            return 0;
        }
        if (dd.bDeviceClass == 0x09) {          /* a hub: never ours */
            log_line("wired %s: %04x:%04x is a USB hub", path, di.udi_vendorNo, di.udi_productNo);
            close(fd);
            remember(path, &sb);
            return 0;
        }
    }
    if (ioctl(fd, USB_GET_FULL_DESC, &gd) != 0) {
        log_line("wired %s: %04x:%04x \"%.32s\": no configuration descriptor (errno %d)",
                 path, di.udi_vendorNo, di.udi_productNo, di.udi_product, errno);
        close(fd);
        remember(path, &sb);
        return 0;
    }
    n = gd.ugd_actlen < (int)sizeof cfg ? gd.ugd_actlen : (int)sizeof cfg;
    gk = gcu_kind(di.udi_vendorNo, di.udi_productNo, access(GCU_DS4_FLAG, F_OK) == 0);
    if (!find_iface(cfg, n, &iface, &kind, &in, &out) &&
        (hid_vendor_ok(di.udi_vendorNo) || gk == GCU_DS4 || gk == GCU_SWITCH)) {
        int hi[4], k2, nh;
        uint8_t hin[4];
        static uint8_t rd[1024];
        nh = find_hid_ifaces(cfg, n, hi, hin, 4);
        for (k2 = 0; k2 < nh; k2++) {
            int rl = hid_report_desc(fd, hi[k2], rd, sizeof rd);
            if (rl <= 0) {
                log_line("wired %s: interface %d: no HID report descriptor (errno %d)", path, hi[k2], errno);
                continue;
            }
            if (try_claim_hid(fd, path, &sb, &di, hi[k2], hin[k2], rd, rl, gk,
                              gk == GCU_SWITCH ? find_out_ep(cfg, n, hi[k2]) : 0)) return 1;
        }
    }
    if (kind == XI_NONE) {
        char cls[96];
        int j, m = 0;
        cls[0] = '\0';
        for (j = 0; j + 9 <= n && m < (int)sizeof cls - 12; j += cfg[j] ? cfg[j] : n)
            if (cfg[j + 1] == 4 && cfg[j] >= 9)
                m += snprintf(cls + m, sizeof cls - (size_t)m, "%s%02x/%02x/%02x", m ? " " : "",
                              cfg[j + 5], cfg[j + 6], cfg[j + 7]);
        log_line("wired %s: %04x:%04x \"%.32s\" not a game pad we drive (interfaces %s)",
                 path, di.udi_vendorNo, di.udi_productNo, di.udi_product, cls[0] ? cls : "none");
        close(fd);
        remember(path, &sb);                    /* not a game pad of ours: never again */
        return 0;
    }
    log_line("wired %s: %04x:%04x rev %04x \"%.32s\", %s on interface %d (in %#x, out %#x)",
             path, di.udi_vendorNo, di.udi_productNo, di.udi_releaseNo, di.udi_product,
             kind == XI_GIP ? "GIP" : "XInput", iface, in, out);

    k = iface;
    if (ioctl(fd, USB_IFACE_DRIVER_ACTIVE, &k) == 0) {
        k = iface;
        if (ioctl(fd, USB_IFACE_DRIVER_DETACH, &k) != 0)
            log_line("wired %s: could not detach the system's driver (errno %d)", path, errno);
    }

    memset(d, 0, sizeof *d);
    d->fd = fd;
    for (i = 0; i < N_X; i++) {
        d->bufp[i][0] = d->buf[i];
        d->ep[i].ppBuffer = d->bufp[i];
        d->ep[i].pLength = d->lenp[i];
        d->ep[i].nFrames = 1;
        d->ep[i].flags = i < N_IN ? (USB_FS_FLAG_SINGLE_SHORT_OK | USB_FS_FLAG_MULTI_SHORT_OK) : 0;
        d->ep[i].timeout = i < N_IN ? USB_FS_TIMEOUT_NONE : 200;
    }
    memset(&init, 0, sizeof init);
    init.pEndpoints = d->ep;
    init.ep_index_max = N_X;
    if (ioctl(fd, USB_FS_INIT, &init) != 0) {
        log_line("wired %s: USB_FS_INIT errno %d", path, errno);
        close(fd);
        return 0;                               /* busy? try again next scan */
    }
    for (i = 0; i < N_X; i++) {
        uint8_t ep = i < N_IN ? in : out;
        if (!ep) continue;
        memset(&op, 0, sizeof op);
        op.ep_index = (uint8_t)i;
        op.ep_no = ep;
        op.max_bufsize = PKT;
        op.max_frames = 1;
        if (ioctl(fd, USB_FS_OPEN, &op) != 0) {
            log_line("wired %s: open endpoint %#x errno %d", path, ep, errno);
            if (i == 0) {
                struct usb_fs_uninit un;
                memset(&un, 0, sizeof un);
                ioctl(fd, USB_FS_UNINIT, &un);
                close(fd);
                return 0;
            }
            if (i == X_OUT) out = 0;
        }
    }

    pthread_mutex_lock(&g_info_lock);
    d->used = 1;
    d->slot = HOST_MAX_PADS + (int)(d - g_dev);
    snprintf(d->path, sizeof d->path, "%s", path);
    xi_setup(&d->xp, kind, di.udi_vendorNo, di.udi_productNo, di.udi_releaseNo);
    pad_state_reset(&d->st);
    d->iface = iface;
    d->ep_in = in;
    d->ep_out = out;
    d->t_open = now_ms();
    pthread_mutex_unlock(&g_info_lock);

    for (i = 0; i < N_IN; i++) x_start(d, i, PKT);
    if (kind == XI_GIP) queue_gip_init(d);
    else {
        uint8_t p[8];
        int m = xi_x360_led_packet(d->slot - HOST_MAX_PADS + 1, p, sizeof p);
        out_push(d, p, m);
    }
    if (g_ev.on_connect)
        g_ev.on_connect(d->slot, d->xp.vid, d->xp.pid, xi_model_name(d->xp.vid, d->xp.pid, kind));
    return 1;
}

/* A HID gamepad interface: the generic (descriptor-driven) parser, no output. */
static int try_claim_hid(int fd, const char *path, const struct stat *sb, const struct usb_device_info *di,
                         int iface, uint8_t in, const uint8_t *rd, int rl, int gk, uint8_t out_ep)
{
    struct usb_fs_init init;
    struct usb_fs_open op;
    wdev *d = NULL;
    int i, k;

    for (i = 0; i < WIRED_MAX_PADS; i++)
        if (!g_dev[i].used) { d = &g_dev[i]; break; }
    if (!d) return 0;
    memset(d, 0, sizeof *d);
    if (gk == GCU_DS4 || gk == GCU_SWITCH) {
        log_line("wired %s: %04x:%04x %s, Ghost-Control driver (%s)", path, di->udi_vendorNo,
                 di->udi_productNo, gcu_name(di->udi_vendorNo, di->udi_productNo),
                 gk == GCU_SWITCH ? "Switch USB handshake" : "DS4 report 0x01");
        if (gk == GCU_SWITCH && !out_ep) {
            log_line("wired %s: no interrupt OUT endpoint on interface %d: cannot do the handshake", path, iface);
            return 0;
        }
    } else if (!generic_setup(d->pctx, rd, rl, di->udi_vendorNo, di->udi_productNo, HID_MAP_DIR)) {
        log_line("wired %s: interface %d is HID but not a gamepad", path, iface);
        return 0;
    }
    log_line("wired %s: %04x:%04x HID gamepad on interface %d (in %#x), %d-byte descriptor",
             path, di->udi_vendorNo, di->udi_productNo, iface, in, rl);
    k = iface;
    if (ioctl(fd, USB_IFACE_DRIVER_ACTIVE, &k) == 0) {
        k = iface;
        if (ioctl(fd, USB_IFACE_DRIVER_DETACH, &k) != 0)
            log_line("wired %s: could not detach the system's HID driver (errno %d)", path, errno);
        else
            log_line("wired %s: took interface %d from the system's HID driver", path, iface);
    }
    /* In DirectInput mode the G7 SE exposes a second HID interface whose
     * descriptor identifies it as a keyboard. The console keeps treating it
     * as a keyboard if that interface is left with the system driver. This
     * exact VID:PID and gamepad interface are known from the live descriptor;
     * do not detach other devices' auxiliary HID interfaces. */
    if (di->udi_vendorNo == 0x3537 && di->udi_productNo == 0x1082 && iface == 0) {
        k = 1;
        if (ioctl(fd, USB_IFACE_DRIVER_ACTIVE, &k) == 0) {
            k = 1;
            if (ioctl(fd, USB_IFACE_DRIVER_DETACH, &k) == 0)
                log_line("wired %s: took GameSir keyboard interface 1 from the system", path);
            else
                log_line("wired %s: could not take GameSir keyboard interface 1 (errno %d)", path, errno);
        }
    }
    d->fd = fd;
    for (i = 0; i < N_X; i++) {
        d->bufp[i][0] = d->buf[i];
        d->ep[i].ppBuffer = d->bufp[i];
        d->ep[i].pLength = d->lenp[i];
        d->ep[i].nFrames = 1;
        d->ep[i].flags = i < N_IN ? (USB_FS_FLAG_SINGLE_SHORT_OK | USB_FS_FLAG_MULTI_SHORT_OK) : 0;
        d->ep[i].timeout = i < N_IN ? USB_FS_TIMEOUT_NONE : 200;
    }
    memset(&init, 0, sizeof init);
    init.pEndpoints = d->ep;
    init.ep_index_max = N_X;
    if (ioctl(fd, USB_FS_INIT, &init) != 0) {
        log_line("wired %s: USB_FS_INIT errno %d", path, errno);
        return 0;
    }
    for (i = 0; i < N_IN; i++) {
        memset(&op, 0, sizeof op);
        op.ep_index = (uint8_t)i;
        op.ep_no = in;
        op.max_bufsize = PKT;
        op.max_frames = 1;
        if (ioctl(fd, USB_FS_OPEN, &op) != 0) {
            struct usb_fs_uninit un;
            log_line("wired %s: open endpoint %#x errno %d", path, in, errno);
            if (i > 0) break;                   /* fewer queued reads, still fine */
            memset(&un, 0, sizeof un);
            ioctl(fd, USB_FS_UNINIT, &un);
            return 0;
        }
    }
    if (out_ep) {
        memset(&op, 0, sizeof op);
        op.ep_index = X_OUT;
        op.ep_no = out_ep;
        op.max_bufsize = PKT;
        op.max_frames = 1;
        if (ioctl(fd, USB_FS_OPEN, &op) != 0) {
            struct usb_fs_uninit un;
            log_line("wired %s: open OUT endpoint %#x errno %d", path, out_ep, errno);
            memset(&un, 0, sizeof un);
            ioctl(fd, USB_FS_UNINIT, &un);
            return 0;
        }
    }
    pthread_mutex_lock(&g_info_lock);
    d->used = 1;
    d->slot = HOST_MAX_PADS + (int)(d - g_dev);
    snprintf(d->path, sizeof d->path, "%s", path);
    d->xp.kind = XI_NONE;
    d->xp.vid = di->udi_vendorNo;
    d->xp.pid = di->udi_productNo;
    d->gk = gk;
    if (gk == GCU_DS4) d->prof = profile_find(0x054C, 0x05C4);          /* the DS4 profile */
    else if (gk == GCU_SWITCH) d->prof = profile_find(0x057E, 0x2009);  /* the Switch profile */
    else d->prof = &generic_profile;
    if (!d->prof) d->prof = &generic_profile;
    pad_state_reset(&d->st);
    d->iface = iface;
    d->ep_in = in;
    d->ep_out = out_ep;
    d->t_open = now_ms();
    pthread_mutex_unlock(&g_info_lock);
    (void)sb;
    for (i = 0; i < N_IN; i++) x_start(d, i, PKT);
    if (gk == GCU_SWITCH) {
        uint8_t pk[4][GCU_PKT];
        int pl[4], m = gcu_switch_begin(&d->sw, d->slot - HOST_MAX_PADS + 1, pk, pl, 4);
        for (i = 0; i < m; i++) out_push(d, pk[i], pl[i]);
        log_line("wired %s: Switch handshake started (OUT %#x)", path, out_ep);
    }
    if (g_ev.on_connect && gk != GCU_NONE && gcu_name(d->xp.vid, d->xp.pid)) {
        g_ev.on_connect(d->slot, d->xp.vid, d->xp.pid, gcu_name(d->xp.vid, d->xp.pid));
    } else if (g_ev.on_connect) {
        static char name[48];
        snprintf(name, sizeof name, "%s", xi_model_name(d->xp.vid, d->xp.pid, XI_GIP));
        if (strstr(name, "(wired)")) snprintf(name, sizeof name, "%s",
            d->xp.vid == 0x3537 ? "GameSir controller (HID)" : "USB gamepad (HID)");
        g_ev.on_connect(d->slot, d->xp.vid, d->xp.pid, name);
    }
    return 1;
}

static void scan(void)
{
    char path[24];
    int bus, addr;
    static int scans;

    if (scans++ == 0) {                 /* once: which USB nodes exist at all */
        char list[384];
        int m = 0;
        DIR *dir = opendir("/dev");
        list[0] = '\0';
        if (dir) {
            struct dirent *e;
            while ((e = readdir(dir)) && m < (int)sizeof list - 24)
                if (strncmp(e->d_name, "ugen", 4) == 0 || strncmp(e->d_name, "usb", 3) == 0)
                    m += snprintf(list + m, sizeof list - (size_t)m, " %s", e->d_name);
            closedir(dir);
            log_line("wired: /dev has:%s", m ? list : " no ugen/usb nodes");
        } else {
            log_line("wired: cannot list /dev (errno %d)", errno);
        }
    }

    for (bus = 0; bus < 8; bus++)
        for (addr = 2; addr <= 31; addr++) {      /* .1 is each bus's root hub */
            snprintf(path, sizeof path, "/dev/ugen%d.%d", bus, addr);
            if (strcmp(path, g_skip) == 0 || in_use(path)) continue;
            try_claim(path);
        }
}

/* ---- reports ----------------------------------------------------------------- */

static int reap(wdev *d)
{
    struct usb_fs_complete done;
    int any = 0;

    for (;;) {
        int i;

        memset(&done, 0, sizeof done);
        if (ioctl(d->fd, USB_FS_COMPLETE, &done) != 0) {
            if (errno != EBUSY) {
                log_line("wired %s: transfer completion failed (errno %d)", d->path, errno);
                return -1;      /* the device may have reset or disappeared */
            }
            break;
        }
        any = 1;
        i = done.ep_index;
        if (i >= N_X) continue;
        d->busy[i] = 0;
        if (i == X_OUT) {
            if (d->ep[i].status) log_line("wired %s: write status %d", d->path, d->ep[i].status);
            out_kick(d);
            continue;
        }
        if (d->ep[i].status == 0 && d->ep[i].aFrames) {
            uint8_t reply[PKT];
            int rl = 0, re = 0, n = (int)d->lenp[i][0];

            if (n > PKT) n = PKT;
            if (!d->first_logged && n > 0) {
                d->first_logged = 1;
                log_line("wired %s: first report after %ld ms (%d bytes, type %#04x)",
                         d->path, now_ms() - d->t_open, n, d->buf[i][0]);
            }
            if (d->prof) {
                const uint8_t *pr = d->buf[i];
                uint8_t tmp[PKT];
                if (d->gk == GCU_SWITCH) {
                    uint8_t pk[8][GCU_PKT];
                    int pl[8], m = gcu_switch_input(&d->sw, d->buf[i], n, tmp, &pr, pk, pl, 8), j;
                    for (j = 0; j < m; j++) out_push(d, pk[j], pl[j]);
                    if (m) log_line("wired %s: Switch handshake reply %#04x %#04x, %d packets sent",
                                    d->path, d->buf[i][0], n > 1 ? d->buf[i][1] : 0, m);
                }
                if (pr && d->prof->parse(d->pctx, pr, n, &d->st) != PARSE_NONE) {
                    d->sent++;
                    pthread_mutex_lock(&g_info_lock);
                    d->web_state = d->st;
                    pthread_mutex_unlock(&g_info_lock);
                    if (g_ev.on_state) g_ev.on_state(d->slot, d->xp.vid, d->xp.pid, &d->st);
                }
            } else if (xi_parse(&d->xp, d->buf[i], n, &d->st, reply, &rl, &re)) {
                d->sent++;
                pthread_mutex_lock(&g_info_lock);
                d->web_state = d->st;
                pthread_mutex_unlock(&g_info_lock);
                if (g_ev.on_state) g_ev.on_state(d->slot, d->xp.vid, d->xp.pid, &d->st);
            }
            if (rl > 0) out_push(d, reply, rl);
            if (re) queue_gip_init(d);
        } else if (d->ep[i].status == 5 /* USB_ERR_CANCELLED */) {
            log_line("wired %s: transfer cancelled on endpoint %#x", d->path, d->ep_in);
            return -1;
        } else if (d->ep[i].status == 22 /* USB_ERR_STALLED */) {
            d->ep[i].flags |= USB_FS_FLAG_CLEAR_STALL;
        }
        if (!x_start(d, i, PKT)) {
            log_line("wired %s: could not restart input transfer (errno %d)", d->path, errno);
            return -1;
        }
        d->ep[i].flags &= (uint16_t)~USB_FS_FLAG_CLEAR_STALL;
    }
    return any;
}

static void *wired_thread(void *arg)
{
    long next_scan = 0;

    (void)arg;
    log_line("wired: looking for USB controllers (Xbox family, HID, Switch Pro, HORI/DS4 family)");
    while (g_run) {
        struct pollfd pf[WIRED_MAX_PADS];
        int map[WIRED_MAX_PADS], n = 0, i, r, timeout;
        long now = now_ms();

        if (g_pause) {
            for (i = 0; i < WIRED_MAX_PADS; i++) dev_close(&g_dev[i], "let go for rest mode");
            g_nseen = 0;
            usleep(200000);
            continue;
        }
        if (now >= next_scan) {
            scan();
            next_scan = now_ms() + T_SCAN;
        }
        for (i = 0; i < WIRED_MAX_PADS; i++)
            if (g_dev[i].used) {
                pf[n].fd = g_dev[i].fd;
                pf[n].events = POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
                pf[n].revents = 0;
                map[n++] = i;
            }
        if (!n) {
            usleep(100000);
            continue;
        }
        timeout = (int)(next_scan - now_ms());
        if (timeout < 0) timeout = 0;
        if (timeout > 200) timeout = 200;       /* notice pause/stop soon */
        if (!g_poll_ok && timeout > 1) timeout = 1;
        r = poll(pf, (nfds_t)n, timeout);
        if (r < 0 && errno != EINTR) {
            log_line("wired: poll errno %d; checking every 1 ms instead", errno);
            g_poll_ok = 0;
        }
        {
            int got = 0;
            for (i = 0; i < n; i++) {
                wdev *d = &g_dev[map[i]];
                int k = reap(d);
                if (k < 0) dev_close(d, "unplugged or stopped answering");
                else got |= k;
            }
            if (r > 0 && !got && g_poll_ok) {
                if (++g_spurious > 2000) {
                    g_poll_ok = 0;
                    log_line("wired: poll() wakes without completions; checking every 1 ms instead");
                }
                usleep(250);
            } else if (got) {
                g_spurious = 0;
            }
        }
    }
    {
        int i;
        for (i = 0; i < WIRED_MAX_PADS; i++) dev_close(&g_dev[i], "stopping");
    }
    return NULL;
}

int wired_start(const wired_events *ev, const char *skip_node)
{
    int i;

    if (g_started) return 1;
    g_ev = *ev;
    snprintf(g_skip, sizeof g_skip, "%s", skip_node ? skip_node : "");
    for (i = 0; i < WIRED_MAX_PADS; i++) g_dev[i].fd = -1;
    g_run = 1;
    g_pause = 0;
    if (pthread_create(&g_thr, NULL, wired_thread, NULL) != 0) {
        g_run = 0;
        log_line("wired: the thread could not start");
        return 0;
    }
    g_started = 1;
    return 1;
}

void wired_pause(int on)
{
    int t;

    g_pause = on ? 1 : 0;
    for (t = 0; on && g_started && wired_count() > 0 && t < 150; t++) usleep(10000);
}

void wired_stop(void)
{
    if (!g_started) return;
    g_run = 0;
    pthread_join(g_thr, NULL);
    g_started = 0;
}

int wired_count(void)
{
    int i, n = 0;

    pthread_mutex_lock(&g_info_lock);
    for (i = 0; i < WIRED_MAX_PADS; i++) n += g_dev[i].used;
    pthread_mutex_unlock(&g_info_lock);
    return n;
}

int wired_describe(int i, char *out, int max)
{
    int k, n = 0, r = 0;

    pthread_mutex_lock(&g_info_lock);
    for (k = 0; k < WIRED_MAX_PADS; k++) {
        if (!g_dev[k].used) continue;
        if (n++ == i) {
            const char *gn = g_dev[k].gk != GCU_NONE ? gcu_name(g_dev[k].xp.vid, g_dev[k].xp.pid) : NULL;
            snprintf(out, (size_t)max, "%s (%04x:%04x, %s, slot %d)",
                     gn ? gn : xi_model_name(g_dev[k].xp.vid, g_dev[k].xp.pid, g_dev[k].xp.kind),
                     g_dev[k].xp.vid, g_dev[k].xp.pid,
                     g_dev[k].gk == GCU_SWITCH ? "Switch USB" : g_dev[k].gk == GCU_DS4 ? "DS4 USB" :
                     g_dev[k].prof ? "HID" : g_dev[k].xp.kind == XI_GIP ? "GIP" : "XInput", g_dev[k].slot + 1);
            r = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_info_lock);
    return r;
}

int wired_snapshot(int i, wired_pad_snapshot *out)
{
    wdev *d;
    if (!out || i < 0 || i >= WIRED_MAX_PADS) return 0;
    pthread_mutex_lock(&g_info_lock);
    d = &g_dev[i];
    if (!d->used) {
        pthread_mutex_unlock(&g_info_lock);
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->slot = d->slot;
    out->vid = d->xp.vid;
    out->pid = d->xp.pid;
    snprintf(out->name, sizeof out->name, "%s",
             d->gk != GCU_NONE && gcu_name(d->xp.vid, d->xp.pid) ? gcu_name(d->xp.vid, d->xp.pid)
                                                                 : xi_model_name(d->xp.vid, d->xp.pid, d->xp.kind));
    snprintf(out->path, sizeof out->path, "%s", d->path);
    out->state = d->web_state;
    pthread_mutex_unlock(&g_info_lock);
    return 1;
}
