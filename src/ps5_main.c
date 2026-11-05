/* SPDX-License-Identifier: GPL-3.0-or-later
 * Master Control (PadBridge/EeveeLution 0.3.16 by X-F1REBALL-X, on AnyPad-PS5
 * and ps5-wireless-pads; USB drivers from Ghost-Control by StonedModder).
 * See CREDITS.md. */
/* PadBridge PS5 -- Bluetooth game controllers on the PlayStation 5.
 *
 * Pads from other consoles and brands connect to the console's own
 * Bluetooth chip, beside the DualSense, and appear to games as DualSense
 * controllers of the main user.
 *
 *   hci_usb.c   the Bluetooth chip, shared with the system's driver
 *   host.c      pairing, reconnection, L2CAP, SDP and HID (Classic)
 *   le.c        the same over Bluetooth LE
 *   profiles.c  each controller's reports
 *   ps5_vpad.c  the virtual DualSense each pad drives
 *   web.c       the menu: a page on the port from the Bloom Core card (8766 without one)
 *   (no console hotkey: a plain payload cannot read the DualSense; the menu opens from the media row)
 *
 * Stability comes first. It only touches what it created: its own Bluetooth
 * links, its own virtual pads, its own files under /data/mastercontrol. When
 * something it depends on fails, it says so (log, notification, the menu)
 * and stays out of the way instead of retrying forever.
 *
 * The menu is started first, before anything that can fail, so that it is
 * there to explain a failure. It is a web page: from a phone or a PC at the
 * address the start-up notification gives, or on the console itself by
 * holding a button combination on the DualSense (L2 + R2 + OPTIONS for 1.5
 * seconds unless /data/mastercontrol/config.ini says otherwise).
 *
 * Stopping: the menu's button, or create /data/mastercontrol/stop. It disconnects
 * its pads, removes its virtual pads, puts back the controller setting it
 * changed, and exits.
 * Rest mode (read from SceSystemStateMgrInfo, see suspend.h): on the way down
 * it lets go of the same things (pads, virtual pads, the chip, the menu) but
 * keeps running, paused; once the console has been awake for a few seconds it
 * opens them again and paired pads reconnect as after a start. Nothing of it is
 * left half-way across a sleep.
 * Pairing: the menu's button, or create /data/mastercontrol/pair.
 */
#include "hci_usb.h"
#include "i18n.h"
#include "instance.h"
#include "host.h"
#include "lock.h"
#include "log.h"
#include "netinfo.h"
#include "ps5_power.h"
#include "ps5_ui.h"
#include "ps5_apps.h"
#include "ps5_sysinfo.h"
#include "profiles.h"
#include "suspend.h"
#include "ps5_vpad.h"
#include "ps5_wired.h"
#include "remap.h"
#include "remap_profile.h"
#include "util.h"
#include "version.h"
#include "web.h"

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>

#include <dirent.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/rtprio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hubcard.h"

/* Master Control 0.4.0: its own folder. PadBridge's (/data/padbridge) is
 * copied in once on first start (pairings, maps, remaps, settings) and
 * never changed or deleted, so going back to PadBridge/EeveeLution works. */
#define STATE_DIR    "/data/mastercontrol"
#define LEGACY_DIR   "/data/padbridge"
#define LEGACY_LOCK  LEGACY_DIR "/padbridge.lock"
#define MIGRATED     STATE_DIR "/.migrated-from-padbridge"
#define OLD_LOG_PATH STATE_DIR "/mastercontrol.log"   /* before 0.4.3, and the fallback */
#define LOG_PATH     HUB_LOG_DIR "/master-control.log" /* the Omnibloom standard's place */
#define PORT_OVERRIDE STATE_DIR "/port"             /* a port number: overrides the card (tests, manual) */
#define AUTOLOAD_TXT "/data/pldmgr/autoload.txt"
#define DB_PATH      STATE_DIR "/pads.db"
#define LOCK_PATH    STATE_DIR "/mastercontrol.lock"
#define CONFIG_PATH  STATE_DIR "/config.ini"
#define PAIR_FLAG    STATE_DIR "/pair"
#define STOP_FLAG    STATE_DIR "/stop"
#define NO_ICON_FLAG STATE_DIR "/no_icon"          /* do not put PadBridge in the media row */
#define LANG_PATH    STATE_DIR "/language"          /* the web page's language, also for notifications */
#define RM_ICON_FLAG STATE_DIR "/remove_icon"      /* take it out, once */
#define ICON_FLAG    STATE_DIR "/icon"             /* in-process install opt-in */
#define LE_RELAXED_FLAG STATE_DIR "/le_relaxed"     /* low-latency patch: do not force 7.5 ms LE intervals */
#define NORMAL_PRIO_FLAG STATE_DIR "/normal_priority" /* low-latency patch: do not raise our priority */
#define REALTIME_FLAG STATE_DIR "/realtime"         /* low-latency patch: realtime scheduling (opt-in) */
#define REMAP_DIR    STATE_DIR "/remap"          /* button maps per model, VVVV_PPPP.map */
#define PAIR_SECONDS 60

#define OPEN_TRIES      5       /* opening Bluetooth: 5 tries, 3 s apart */
#define RAISED_TRIES    2       /* ... the first two with raised credentials */

/* Findable in the binary: strings master-control.elf | grep mastercontrol-version */
static const char g_version_tag[] __attribute__((used)) = "mastercontrol-version " PADBRIDGE_VERSION;
#define PADBRIDGE_AUTHOR "X-F1REBALL-X"
static const char g_author_tag[] __attribute__((used)) = "padbridge-author " PADBRIDGE_AUTHOR;

static volatile sig_atomic_t g_stop;
static volatile int g_web_stop;         /* the menu's stop button */
static volatile int g_retry;            /* the menu's retry button */
static volatile int g_bt_state = WEB_BT_TRYING;
static char g_bt_reason[200];
static const char *g_bt_reason_key = "";       /* the same, as a key the page translates */

static host_t *g_host;                  /* NULL until Bluetooth is up */
static web_t *g_web;
static int g_vpad_ok;                   /* virtual pads are possible */
static int g_bt_attempt;                /* Bluetooth tries made in this round */
static long g_bt_next;                  /* when the next one is due */
static int g_bt_plain_creds;            /* the chip answered only without raised credentials */
static int g_bt_after_rest;             /* this round follows rest mode: tries back off */
static int g_web_tries;                 /* menu tries left after rest mode */
static int g_port = HUB_DEFAULT_PORT;   /* the menu's port: Bloom Core card, else 8766 */
static int g_runtime_written;           /* /data/hub/runtime/master-control.json is ours to remove */
static long g_web_next;

/* Only sets the flag: the main loop does the stopping (and so the full
 * cleanup, which is what lets a newer copy take over after SIGTERM). */
static void on_signal(int sig)
{
    int saved = errno;

    (void)sig;
    g_stop = 1;
    errno = saved;
}

static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_signal;          /* no SA_RESTART: a signal cuts a sleep short */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);      /* a browser closing early */
}

/* The lock is held: if by an older copy that is running, ask it to stop and
 * take its place. Returns 1 with the lock held. */
static int take_over(void)
{
    static const char *const what[] = {
        "gone", "not a PadBridge process, left alone", "could not be signalled",
        "still running after the wait", "stop requested meanwhile",
    };
    long old = lock_owner(LOCK_PATH);
    int r;

    if (old <= 0) return lock_take(LOCK_PATH);          /* released meanwhile */
    log_line("another instance is running (pid %ld): asking it to stop", old);
    r = instance_takeover(old, instance_system_ops(&g_stop), INSTANCE_WAIT_MS, INSTANCE_POLL_MS);
    log_line("previous instance: %s", what[r]);
    if (r == TAKEOVER_TIMEOUT) {
        /* 0.3.2: confirmed to be PadBridge but stuck (e.g. a copy that hung
         * loading a system library): force it, then take the lock. */
        int t;
        log_line("previous instance did not stop: forcing it (SIGKILL)");
        kill((pid_t)old, SIGKILL);
        for (t = 0; t < 40 && !lock_take(LOCK_PATH); t++) usleep(50000);
        if (t < 40) {
            log_line("took over from pid %ld (forced)", old);
            return 1;
        }
        /* Killed but not yet reaped by its parent (a zombie still answers
         * kill(pid, 0)): the lock is ours to clear. */
        if (lock_owner(LOCK_PATH) == old) {
            unlink(LOCK_PATH);
            if (lock_take(LOCK_PATH)) {
                log_line("took over from pid %ld (forced, lock cleared)", old);
                return 1;
            }
        }
        log_line("forced the previous instance, but the lock could not be taken");
        return 0;
    }
    if (r != TAKEOVER_GONE) return 0;
    if (!lock_take(LOCK_PATH)) {
        log_line("previous instance gone, but the lock could not be taken");
        return 0;
    }
    log_line("took over from pid %ld", old);
    return 1;
}

/* ---- menu shortcut ------------------------------------------------------------
 * Holding View + Menu (Back + Start on 360-type pads) for 2 s on any bridged
 * controller opens the menu in the console's browser. Read before the remap,
 * so it works whatever the buttons are mapped to. Called from both the main
 * loop and the wired thread: it only raises a flag the main loop acts on. */
#define MENU_COMBO   (PAD_TOUCHPAD | PAD_OPTIONS)
#define MENU_HOLD_MS 2000
static long g_combo_since[VPAD_SLOTS];
static volatile int g_open_menu;

/* 0.3.4: no-phone pairing. A Bluetooth pairing window opens by itself when
 * a wired controller is unplugged (hold its pair button to keep playing
 * without the cable) and at start-up when nothing is paired yet. The
 * controller's pair button still has to be held once: no controller
 * accepts a Bluetooth pairing over its cable. */
#define AUTO_PAIR_SECONDS 60
#define NO_AUTO_PAIR_FLAG STATE_DIR "/no_auto_pair"
static volatile int g_pair_req;          /* 1: unplugged, 2: start-up */

static void menu_combo_check(int slot, const pad_state *st)
{
    long now;

    if (slot < 0 || slot >= VPAD_SLOTS) return;
    if ((st->buttons & MENU_COMBO) != MENU_COMBO) {
        g_combo_since[slot] = 0;
        return;
    }
    now = now_ms();
    if (!g_combo_since[slot]) g_combo_since[slot] = now;
    else if (g_combo_since[slot] > 0 && now - g_combo_since[slot] >= MENU_HOLD_MS) {
        g_combo_since[slot] = -1;       /* once per hold */
        g_open_menu = 1;
    }
}

static void menu_open_if_asked(void)
{
    char url[64];

    if (!g_open_menu) return;
    g_open_menu = 0;
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", g_port);
    log_line("menu shortcut: opening %s", url);
    ui_open_browser(url);
}

/* ---- virtual pads follow the controllers ---------------------------------- */

/* Each slot's model name, kept for its disconnection notice. */
static char g_slot_name[HOST_MAX_PADS][48];

static const char *slot_name(int slot)
{
    return slot >= 0 && slot < HOST_MAX_PADS && g_slot_name[slot][0] ? g_slot_name[slot] : tr(MSG_CONTROLLER);
}

/* Each slot's model, for the remap layer. */
static uint16_t g_slot_id[HOST_MAX_PADS][2];

static void on_connect(void *ud, int slot, const pad_info *info)
{
    (void)ud;
    if (slot >= 0 && slot < HOST_MAX_PADS) {
        snprintf(g_slot_name[slot], sizeof g_slot_name[slot], "%s",
                 pad_display_name(info->vid, info->pid, info->profile));
        g_slot_id[slot][0] = info->vid;
        g_slot_id[slot][1] = info->pid;
    }
    remap_note_device(info->vid, info->pid, pad_display_name(info->vid, info->pid, info->profile));
    if (vpad_add(slot)) notify(tr(MSG_CONNECTED), slot_name(slot), slot + 1);
    else notify(tr(MSG_CONNECTED_NOVPAD), slot_name(slot), slot + 1);
}

static void on_state(void *ud, int slot, const pad_state *st)
{
    pad_state s = *st;

    (void)ud;
    menu_combo_check(slot, st);
    if (slot >= 0 && slot < HOST_MAX_PADS) remap_apply(g_slot_id[slot][0], g_slot_id[slot][1], &s);
    vpad_update(slot, &s);
}

/* The menu's PS button: the same as holding PS on that pad. */
#define PS_HOLD_MS 700
static int web_press_ps(int slot)
{
    return g_vpad_ok && vpad_press_ps(slot, PS_HOLD_MS);
}

static void on_disconnect(void *ud, int slot)
{
    (void)ud;
    vpad_remove(slot);
    notify(tr(MSG_DISCONNECTED), slot_name(slot), slot + 1);
}

/* ---- wired USB controllers (low-latency patch) ------------------------------
 * Called from the wired thread; the virtual-pad layer has its own lock. */
#define NO_WIRED_FLAG STATE_DIR "/no_wired"        /* do not look for USB controllers */
static char g_wired_name[WIRED_MAX_PADS][48];
static uint16_t g_wired_vid[WIRED_MAX_PADS];

static void wired_on_connect(int slot, uint16_t vid, uint16_t pid, const char *name)
{
    int w = slot - HOST_MAX_PADS;

    if (w < 0 || w >= WIRED_MAX_PADS) return;
    snprintf(g_wired_name[w], sizeof g_wired_name[w], "%s (USB)", name);
    g_wired_vid[w] = vid;
    remap_note_device(vid, pid, name);
    if (vpad_add(slot)) notify(tr(MSG_CONNECTED), g_wired_name[w], slot + 1);
    else notify(tr(MSG_CONNECTED_NOVPAD), g_wired_name[w], slot + 1);
}

static void wired_on_state(int slot, uint16_t vid, uint16_t pid, const pad_state *st)
{
    pad_state s = *st;

    menu_combo_check(slot, st);
    remap_apply(vid, pid, &s);
    vpad_update(slot, &s);
}

static void wired_on_disconnect(int slot)
{
    int w = slot - HOST_MAX_PADS;
    uint16_t vid = w >= 0 && w < WIRED_MAX_PADS ? g_wired_vid[w] : 0;

    vpad_remove(slot);
    if (w >= 0 && w < WIRED_MAX_PADS) notify(tr(MSG_DISCONNECTED), g_wired_name[w], slot + 1);
    /* A wired-only GameSir, Razer, or generic USB pad cannot start Bluetooth
     * pairing when unplugged. Offer the shortcut only to Microsoft Xbox pads. */
    if (vid == 0x045e && access(NO_AUTO_PAIR_FLAG, F_OK) != 0)
        g_pair_req = 1;     /* acted on by the main loop */
}

/* Main loop only: tells the player the one step left after a new pairing. */
static void paired_watch(void)
{
    static const host_t *seen_host;
    static int last = -1;
    int n;

    if (!g_host) return;
    n = host_paired_count(g_host);
    if (g_host != seen_host) {          /* a new host (start, rest mode): just note the count */
        seen_host = g_host;
        last = n;
        return;
    }
    if (n > last) {
        host_pair_stop(g_host);         /* done: stop searching */
        notify("%s", tr(MSG_PAIRED_NOW));
    }
    last = n;
}

/* Main loop only (the host is not thread-safe). */
static void auto_pair_if_asked(void)
{
    int why = g_pair_req;

    if (!why) return;
    g_pair_req = 0;
    if (!g_host || host_pairing_left(g_host) > 0) return;
    host_pair(g_host, AUTO_PAIR_SECONDS);
    log_line("auto pairing: %s, window open %d s", why == 1 ? "a wired controller was unplugged" : "nothing paired yet",
             AUTO_PAIR_SECONDS);
    if (why == 1)
        notify(tr(MSG_AUTOPAIR_UNPLUGGED), AUTO_PAIR_SECONDS);
    else
        notify(tr(MSG_AUTOPAIR_NONE), AUTO_PAIR_SECONDS);
}

static void wired_begin(void)
{
    static const wired_events ev = { wired_on_connect, wired_on_state, wired_on_disconnect };

    if (access(NO_WIRED_FLAG, F_OK) == 0) {
        log_line("wired: off (no_wired flag)");
        return;
    }
    wired_start(&ev, "/dev/ugen0.2");          /* never the Bluetooth chip */
}

/* ---- Bluetooth, one try at a time ------------------------------------------ */

/* The host calls this whenever it waits for the chip, so the menu answers
 * even while a Bluetooth try is in progress (it used to freeze for ~5 s each
 * time, and the browser's pile-up of connections got "busy"). */
static void on_idle(void *ud)
{
    (void)ud;
    web_poll(g_web);
}

static void bt_begin(int after_rest)
{
    g_bt_after_rest = after_rest;
    g_bt_attempt = 0;
    g_bt_next = 0;
    g_bt_reason[0] = '\0';
    g_bt_reason_key = "";
    g_bt_state = WEB_BT_TRYING;
    if (g_vpad_ok) vpad_creds_raise();
}

/* One try. The controller may answer only to a process that does not carry
 * raised credentials (not known), so the first
 * tries are made as they are and the rest with the original ones. Which
 * worked is logged. Virtual pads raise them again when they need to. */
static void bt_try(long now)
{
    host_events ev = { on_connect, on_state, on_disconnect, NULL, on_idle };
    hci_t hci;
    int attempt = ++g_bt_attempt;

    if (attempt == RAISED_TRIES + 1 && g_vpad_ok) {
        log_line("Bluetooth silent with raised credentials; trying with the original ones");
        if (vpad_creds_restore()) g_bt_plain_creds = 1;
    }
    if (hci_usb_open(&hci)) {
        g_host = host_open(hci, DB_PATH, &ev);
        if (g_host && access(LE_RELAXED_FLAG, F_OK) == 0) {
            host_set_le_relaxed(g_host, 1);
            log_line("le: le_relaxed flag set, intervals 7.5..15 ms");
        }
        if (g_host) {
            g_bt_state = WEB_BT_OK;
            log_line("Bluetooth ready after %d tr%s%s", attempt, attempt == 1 ? "y" : "ies",
                     g_bt_plain_creds ? ", answering only with the original credentials" : "");
            notify(tr(MSG_BT_READY), host_paired_count(g_host));
            if (host_paired_count(g_host) == 0 && attempt == 1 && access(NO_AUTO_PAIR_FLAG, F_OK) != 0)
                g_pair_req = 2;         /* first run: pair without the menu */
            return;
        }
        hci.ops->close(hci.ctx);
    }
    log_line("Bluetooth not ready (try %d of %d)", attempt, OPEN_TRIES);
    if (attempt >= OPEN_TRIES) {
        g_bt_state = WEB_BT_FAILED;
        snprintf(g_bt_reason, sizeof g_bt_reason,
                 "The Bluetooth chip does not answer (%d tries, with and without raised credentials). "
                 "Details in the log, 'diag' lines.", attempt);
        g_bt_reason_key = "nochip";
        log_line("Bluetooth unavailable: %s", g_bt_reason);
        notify("%s", tr(MSG_BT_NO_ANSWER));
        return;
    }
    /* After rest mode the chip may still be waking: wait longer each time. */
    g_bt_next = now + (g_bt_after_rest ? suspend_backoff_ms(attempt) : 3000);
}

static void bt_lost(const char *why)
{
    log_line("Bluetooth lost: %s", why);
    host_close(g_host);                 /* drops every pad and its virtual pad */
    g_host = NULL;
    g_bt_state = WEB_BT_FAILED;
    snprintf(g_bt_reason, sizeof g_bt_reason, "Bluetooth lost (%s). Press Retry.", why);
    g_bt_reason_key = "lost";
    notify("%s", tr(MSG_BT_LOST));
}

/* ---- rest mode ------------------------------------------------------------- */

static web_cfg g_wc;                    /* the menu's settings, to start it again */

/* The menu, and the notification with its address (which may have changed
 * across a sleep). After rest mode a failure is tried again a few times. */
static void web_open(long now)
{
    char ip[16], url[64];

    g_web = web_start(&g_wc, g_port);
    local_ip(ip);
    snprintf(url, sizeof url, "http://%s:%d", ip, g_port);
    if (g_web) {
        g_web_tries = 0;
        /* Bloom Core reads where we really are from here (never from the card,
         * which only Setup writes). Rewritten on every start: pids are reused. */
        if (hubcard_runtime_write(HUB_RUNTIME_PATH, g_port, (long)getpid(), (long)time(NULL), PADBRIDGE_VERSION)) {
            g_runtime_written = 1;
        } else {
            log_line("hub: could not write %s", HUB_RUNTIME_PATH);
        }
        notify(tr(MSG_READY), PADBRIDGE_VERSION, url);
        return;
    }
    if (g_web_tries > 0 && --g_web_tries > 0) {
        g_web_next = now + suspend_backoff_ms(RESUME_TRIES - g_web_tries);
        log_line("menu not available yet; %d tr%s left", g_web_tries, g_web_tries == 1 ? "y" : "ies");
        return;
    }
    g_web_tries = 0;
    notify(tr(MSG_MENU_UNAVAILABLE), PADBRIDGE_VERSION, g_port);
}

/* On the way to rest mode: the same teardown as a stop, minus the exit and
 * the lock. pads.db (pairings) is not touched. Assumption: whether the chip's
 * USB handle, the virtual pads or the menu's socket stay valid across a sleep
 * is not known, so everything is closed now and opened afresh on waking. */
static void rest_suspend(void)
{
    log_line("rest mode: suspending (pads, Bluetooth, menu)");
    notify("%s", tr(MSG_REST_MODE));
    wired_pause(1);                     /* the wired thread lets go of its pads */
    host_close(g_host);                 /* our links; removes our virtual pads; closes the chip */
    g_host = NULL;
    if (g_vpad_ok) {
        g_bt_state = WEB_BT_TRYING;     /* tried again on waking */
        vpad_creds_restore();
    }
    web_stop(g_web);
    g_web = NULL;
    g_web_tries = 0;
    log_line("rest mode: suspended, waiting for the console to wake");
}

/* Awake for the grace period: open again what rest_suspend closed, through
 * the start-up paths, each with its bounded tries. */
static void rest_resume(long now)
{
    log_line("rest mode over: resuming after %d ms awake", SUSPEND_GRACE_MS);
    g_web_tries = RESUME_TRIES;
    web_open(now);
    if (g_vpad_ok) bt_begin(1);         /* paired pads then reconnect by themselves */
    else log_line("rest mode over: no virtual pads, Bluetooth left alone");
    wired_pause(0);
}

/* ---- flags ---------------------------------------------------------------- */

static void check_flags(long now)
{
    static long last_second;

    if (now / 1000 == last_second) return;
    last_second = now / 1000;
    if (access(STOP_FLAG, F_OK) == 0) {
        unlink(STOP_FLAG);
        g_stop = 1;
    }
    if (g_host && access(PAIR_FLAG, F_OK) == 0) {
        unlink(PAIR_FLAG);
        host_pair(g_host, PAIR_SECONDS);
        notify(tr(MSG_PAIRING), PAIR_SECONDS);
    }
}

/* Low-latency patch: the bridge sleeps almost all the time and must wake
 * the instant a report arrives, even while a game keeps every core busy.
 * Default: the best timeshare priority (nice -20). With the file
 * /data/mastercontrol/realtime: FreeBSD realtime class, which pre-empts
 * ordinary threads; safe because the loop never spins (it sleeps in poll or
 * usleep between reports), but opt-in. /data/mastercontrol/normal_priority
 * turns all of this off. Failures are logged and ignored. */
static void raise_priority(void)
{
    if (access(NORMAL_PRIO_FLAG, F_OK) == 0) {
        log_line("priority: left as is (normal_priority flag)");
        return;
    }
    if (setpriority(PRIO_PROCESS, 0, -20) == 0) log_line("priority: nice -20");
    else log_line("priority: setpriority failed (errno %d)", errno);
    if (access(REALTIME_FLAG, F_OK) == 0) {
        struct rtprio rtp;
        rtp.type = RTP_PRIO_REALTIME;
        rtp.prio = 16;                  /* middle of the realtime range (0 is highest) */
        if (rtprio(RTP_SET, 0, &rtp) == 0) log_line("priority: realtime 16");
        else log_line("priority: rtprio failed (errno %d)", errno);
    }
}

/* ---- Master Control: first-start copy of PadBridge's folder ---------------- */

static int copy_file(const char *from, const char *to)
{
    char buf[8192];
    int in, out, ok = 1;
    ssize_t n;

    if (access(to, F_OK) == 0) return 1;            /* never overwrite */
    in = open(from, O_RDONLY);
    if (in < 0) return 0;
    out = open(to, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (out < 0) { close(in); return 0; }
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t)n) != n) { ok = 0; break; }
    if (n < 0) ok = 0;
    close(in);
    close(out);
    return ok;
}

static int skip_legacy(const char *name)
{
    static const char *const skip[] = {
        "padbridge.lock", "padbridge.log", "padbridge.log.1", "padbridge.log.old",
        "stop", "pair", "remove_icon", NULL,
    };
    int i;
    if (name[0] == '.') return 1;
    for (i = 0; skip[i]; i++) if (strcmp(name, skip[i]) == 0) return 1;
    return 0;
}

static int copy_dir(const char *from, const char *to, int depth)
{
    DIR *dir = opendir(from);
    struct dirent *e;
    int n = 0;

    if (!dir) return 0;
    mkdir(to, 0755);
    while ((e = readdir(dir))) {
        char a[256], b[256];
        struct stat st;
        if (skip_legacy(e->d_name)) continue;
        snprintf(a, sizeof a, "%s/%s", from, e->d_name);
        snprintf(b, sizeof b, "%s/%s", to, e->d_name);
        if (stat(a, &st) != 0) continue;
        if (S_ISREG(st.st_mode)) n += copy_file(a, b);
        else if (S_ISDIR(st.st_mode) && depth < 2) n += copy_dir(a, b, depth + 1);
    }
    closedir(dir);
    return n;
}

static void migrate_from_padbridge(void)
{
    int n;
    FILE *f;

    if (access(MIGRATED, F_OK) == 0) return;
    if (access(LEGACY_DIR, F_OK) != 0) {
        log_line("no PadBridge folder to copy settings from");
    } else {
        n = copy_dir(LEGACY_DIR, STATE_DIR, 0);
        log_line("copied %d files from %s (pairings, maps, settings); the originals are untouched", n, LEGACY_DIR);
    }
    if ((f = fopen(MIGRATED, "w"))) { fputs("1\n", f); fclose(f); }
}

/* A PadBridge or EeveeLution copy (same thread name) still running from its
 * own folder: ask it to stop first, so two copies never share the Bluetooth
 * chip or the virtual pads. Returns 0 if it is still running. */
static int stop_legacy_padbridge(void)
{
    long old = lock_owner(LEGACY_LOCK);
    int r;

    if (old <= 0 || old == (long)getpid()) return 1;
    log_line("PadBridge/EeveeLution may be running (pid %ld): asking it to stop", old);
    r = instance_takeover(old, instance_system_ops(&g_stop), INSTANCE_WAIT_MS, INSTANCE_POLL_MS);
    if (r == TAKEOVER_GONE) { log_line("PadBridge/EeveeLution: not running (any more)"); return 1; }
    if (r == TAKEOVER_NOT_OURS) { log_line("PadBridge lock names pid %ld, not a PadBridge process: ignored", old); return 1; }
    log_line("PadBridge/EeveeLution did not stop (result %d): not starting", r);
    return 0;
}

int main(void)
{
    const char *why = "stop requested";
    long last_power = 0;
    suspend_t rest;

    mkdir(STATE_DIR, 0755);
    /* The log lives where the Omnibloom standard puts every app's log; if
     * that folder cannot be made, in our own folder as before. */
    if (!hubcard_mkdirs(HUB_LOG_DIR) || !log_open(LOG_PATH)) log_open(OLD_LOG_PATH);
    log_line("Master Control %s - PadBridge by %s, USB drivers from Ghost-Control by StonedModder",
             PADBRIDGE_VERSION, PADBRIDGE_AUTHOR);
    {
        int source;
        const char *why = "";
        g_port = hubcard_resolve_port(PORT_OVERRIDE, HUB_CARD_PATH, &source, &why);
        if (source == HUB_PORT_CARD) {
            log_line("hub: menu port %d from the Bloom Core card", g_port);
        } else if (source == HUB_PORT_OVERRIDE) {
            log_line("hub: menu port %d from %s (overrides the card)", g_port, PORT_OVERRIDE);
        } else {
            int al = hubcard_autoload_mentions(AUTOLOAD_TXT);
            log_line("hub: %s (%s): menu on the default port %d", HUB_CARD_PATH, why, g_port);
            if (al == 1)
                log_line("hub: started from pldmgr's autoload without an Omnibloom card - the menu moved "
                         "from 8095 to %d in 0.4.3 (8095 clashes with OmniPad-PS5); install with Omnibloom "
                         "Setup to get a card", g_port);
        }
    }
    remap_init(REMAP_DIR);
    i18n_load(LANG_PATH);
    log_line("language: %s", i18n_code(i18n_get()));
    (void)g_version_tag;
    (void)g_author_tag;

    install_signals();                  /* before the takeover: a stop while waiting counts */
    if (!stop_legacy_padbridge()) {
        notify("%s", tr(MSG_ALREADY_RUNNING));
        log_close();
        return 1;
    }
    migrate_from_padbridge();           /* after PadBridge stopped: its files are settled */
    remap_init(REMAP_DIR);              /* again: the copied remaps */
    i18n_load(LANG_PATH);               /* and the copied language choice */
    if (!lock_take(LOCK_PATH) && !take_over()) {
        log_line("another instance is running");
        notify("%s", tr(MSG_ALREADY_RUNNING));
        log_close();
        return 1;
    }
    unlink(STOP_FLAG);                  /* an old request is not for us */
    syscall(SYS_thr_set_name, -1, "padbridge");    /* how others find us */
    raise_priority();

    /* The menu first: it is what explains everything that follows. */
    memset(&g_wc, 0, sizeof g_wc);
    g_wc.host = &g_host;
    g_wc.stop = &g_web_stop;
    g_wc.retry = &g_retry;
    g_wc.bt_state = &g_bt_state;
    g_wc.bt_reason = g_bt_reason;
    g_wc.bt_reason_key = &g_bt_reason_key;
    g_wc.lang_path = LANG_PATH;
    g_wc.log_path = log_path() ? log_path() : OLD_LOG_PATH;
    g_wc.port = g_port;
    g_wc.token_path = HUB_TOKEN_PATH;
    g_wc.profiles_path = REMAP_PROFILE_DIR;
    g_wc.autopair_off_path = NO_AUTO_PAIR_FLAG;
    g_wc.version = PADBRIDGE_VERSION;
    g_wc.press_ps = web_press_ps;
    g_wc.wired_snapshot = wired_snapshot;
    g_wc.virtual_status = vpad_status;
    web_open(now_ms());

    sysinfo_log();

    /* Keep in-process installation opt-in. An earlier install stalled this
     * bridge on firmware 13.60; the standalone helper is the normal path. */
    if (access(RM_ICON_FLAG, F_OK) == 0) {
        apps_remove_launcher();
        unlink(RM_ICON_FLAG);
    } else if (access(ICON_FLAG, F_OK) == 0 && access(NO_ICON_FLAG, F_OK) != 0) {
        apps_install_launcher(g_port);
    } else {
        log_line("launcher: media-row icon handled by standalone installer");
    }

    /* Without virtual pads there is nothing to connect controllers to:
     * Bluetooth is not touched at all, and the menu says why. */
    g_vpad_ok = vpad_init();
    if (!g_vpad_ok) {
        g_bt_state = WEB_BT_FAILED;
        snprintf(g_bt_reason, sizeof g_bt_reason,
                 "Virtual controllers cannot be created on this system (see the log). Bluetooth was not touched.");
        g_bt_reason_key = "novpad";
        notify("%s", tr(MSG_NO_VPAD));
    } else {
        bt_begin(0);
        wired_begin();
    }

    suspend_init(&rest);
    while (!g_stop && !g_web_stop) {
        long now;

        /* Paused for rest mode: read the power state now and then, and
         * nothing else but the stop flag and what a removed virtual pad
         * still has pending. A signal cuts the sleep short. */
        if (rest.state != SUS_ACTIVE) {
            usleep((useconds_t)suspend_poll_ms(&rest) * 1000);
            now = now_ms();
            if (g_vpad_ok) vpad_poll(now);
            if (suspend_step(&rest, power_state(), now) == SUS_DO_RESUME) rest_resume(now);
            check_flags(now);
            continue;
        }

        if (g_host) host_poll(g_host, 4);
        else usleep(4000);
        now = now_ms();
        web_poll(g_web);
        if (g_vpad_ok) vpad_poll(now);
        menu_open_if_asked();
        auto_pair_if_asked();
        paired_watch();

        /* Rest mode on its way: let go cleanly before it, not after. */
        if (now - last_power >= suspend_poll_ms(&rest)) {
            last_power = now;
            if (suspend_step(&rest, power_state(), now) == SUS_DO_SUSPEND) {
                rest_suspend();
                continue;
            }
        }
        if (!g_web && g_web_tries > 0 && now >= g_web_next) web_open(now);

        /* The Bluetooth: tries, loss, and the menu's retry button. */
        if (g_bt_state == WEB_BT_TRYING && now >= g_bt_next) bt_try(now);
        if (g_host && host_transport_lost(g_host)) bt_lost("the chip stopped answering");
        if (g_retry) {
            g_retry = 0;
            if (g_vpad_ok && g_bt_state == WEB_BT_FAILED) {
                log_line("retrying Bluetooth at the user's request");
                bt_begin(0);
            } else {
                log_line("retry ignored (virtual pads not available, or not failed)");
            }
        }

        check_flags(now);
    }
    if (g_web_stop) why = "stop from the menu";
    else if (g_stop && strcmp(why, "stop requested") == 0) why = "stop requested (flag or signal)";

    log_line("stopping: %s", why);
    web_stop(g_web);
    wired_stop();                       /* its pads, and their virtual pads */
    host_close(g_host);                 /* our links; removes our virtual pads */
    g_host = NULL;
    if (g_vpad_ok) {
        vpad_creds_restore();           /* leave the credentials as found */
    }
    if (g_runtime_written && !hubcard_runtime_remove(HUB_RUNTIME_PATH))
        log_line("hub: could not remove %s", HUB_RUNTIME_PATH);
    lock_release();
    log_line("stopped");
    log_close();
    notify("%s", tr(MSG_STOPPED));
    return 0;
}
