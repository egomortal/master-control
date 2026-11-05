#include "web.h"
#include "ps5_vpad.h"
#include "i18n.h"
#include "log.h"
#include "profiles.h"
#include "remap.h"
#include "remap_profile.h"
#include "hubcard.h"
#include "util.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WEB_CLIENTS     8
#define REQ_MAX         8192
#define T_REQUEST       3000    /* ms for a client to send its request */
#define LOG_TAIL        6144

#include "web_tools.h"

extern const unsigned char web_page_html[];
extern const unsigned int web_page_html_len;
extern const unsigned char web_classic_html[];   /* the previous EeveeLution menu, at /classic */
extern const unsigned int web_classic_html_len;
extern const unsigned char web_overlay_html[];    /* the quick overlay, at /overlay */
extern const unsigned int web_overlay_html_len;
extern const unsigned char icon_png_data[];
extern const unsigned int icon_png_data_len;

typedef struct {
    int fd;
    int local;                          /* the peer is on the local network */
    int loopback;                       /* the peer is this console (127.x) */
    long t_start;
    int len;
    char buf[REQ_MAX + 1];
} client;

struct web {
    web_cfg cfg;
    int fd;
    client c[WEB_CLIENTS];
};

static int g_allow_any_peer;            /* tests only */

/* ---- helpers ------------------------------------------------------------- */

/* MC-02: 0 only when the socket really is non-blocking now. */
static int set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL);

    if (fl < 0) return -1;
    if (!(fl & O_NONBLOCK) && fcntl(fd, F_SETFL, fl | O_NONBLOCK) != 0) return -1;
    fl = fcntl(fd, F_GETFL);
    return (fl >= 0 && (fl & O_NONBLOCK)) ? 0 : -1;
}

/* Sends all of it, giving up on a client too slow to take it. */
static void send_all(int fd, const char *p, size_t n)
{
    long deadline = now_ms() + 1000;

    while (n > 0) {
        ssize_t k = send(fd, p, n, 0);
        if (k > 0) {
            p += k;
            n -= (size_t)k;
        } else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now_ms() < deadline) {
            usleep(1000);
        } else {
            return;
        }
    }
}

static void respond(int fd, int code, const char *type, const char *body, size_t len)
{
    char head[256];
    const char *msg = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 429 ? "Too Many Requests" : "Error";
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                     "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                     "Connection: close\r\n\r\n",
                     code, msg, type, (unsigned)len);
    send_all(fd, head, (size_t)n);
    send_all(fd, body, len);
}

static void respond_text(int fd, int code, const char *text)
{
    respond(fd, code, "application/json", text, strlen(text));
}

/* The value of a header, name compared without regard to case. */
static int header(const char *req, const char *name, char *out, size_t max)
{
    size_t nl = strlen(name);
    const char *p = strstr(req, "\r\n");

    while (p && p[2] != '\r') {
        const char *line = p + 2, *end = strstr(line, "\r\n");
        if (!end) return 0;
        if ((size_t)(end - line) > nl && line[nl] == ':' && strncasecmp(line, name, nl) == 0) {
            const char *v = line + nl + 1;
            size_t k = 0;
            while (*v == ' ') v++;
            while (v < end && k + 1 < max) out[k++] = *v++;
            out[k] = '\0';
            return 1;
        }
        p = end;
    }
    return 0;
}

/* A Host that is an IPv4 address (or localhost), with an optional port: not
 * a name. A web page on the Internet cannot make a browser use one to reach
 * the console (DNS rebinding hands the browser the console's address under
 * the attacker's name, and the name is what arrives here). */
static int host_is_literal(const char *h)
{
    unsigned a, b, c, d;
    char tail;
    int n;

    if (strncmp(h, "localhost", 9) == 0 && (h[9] == '\0' || h[9] == ':')) return 1;
    n = sscanf(h, "%3u.%3u.%3u.%3u%c", &a, &b, &c, &d, &tail);
    if (n == 4) return a < 256 && b < 256 && c < 256 && d < 256;
    return n == 5 && tail == ':' && a < 256 && b < 256 && c < 256 && d < 256;
}

/* Loopback or a private range: the page is for the local network only. */
static int peer_is_local(const struct sockaddr_in *sin)
{
    uint32_t a = ntohl(sin->sin_addr.s_addr);

    return (a >> 24) == 127 || (a >> 24) == 10 || (a >> 20) == 0xAC1 ||
           (a >> 16) == 0xC0A8 || (a >> 16) == 0xA9FE;
}

static int peer_is_loopback(const struct sockaddr_in *sin)
{
    return (ntohl(sin->sin_addr.s_addr) >> 24) == 127;
}

static int parse_addr(const char *s, unsigned char a[6])
{
    unsigned v[6];
    int i;

    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return 0;
    for (i = 0; i < 6; i++) a[5 - i] = (unsigned char)v[i];   /* shown MSB first */
    return 1;
}

static void fmt_addr(char *out, const unsigned char *a)
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

/* ---- API --------------------------------------------------------------------- */

/* Copies `s` into out as JSON string content: quotes, backslashes and
 * control characters are dropped, so nothing can break the document. */
static void json_text(char *out, size_t max, const char *s)
{
    size_t n = 0;

    for (; s && *s && n + 1 < max; s++)
        if ((unsigned char)*s >= 0x20 && *s != '"' && *s != '\\') out[n++] = *s;
    out[n] = '\0';
}

static const char *virtual_name(const web_t *w, int slot)
{
    int st = w->cfg.virtual_status ? w->cfg.virtual_status(slot) : VPAD_STATUS_READY;
    switch (st) {
    case VPAD_STATUS_WAITING: return "waiting";
    case VPAD_STATUS_STARTING: return "starting";
    case VPAD_STATUS_READY: return "ready";
    case VPAD_STATUS_FAILED: return "failed";
    default: return "none";
    }
}

static void api_state(web_t *w, int fd)
{
    static const struct { uint32_t bit; const char *name; } names[] = {
        { PAD_CROSS, "cross" }, { PAD_CIRCLE, "circle" }, { PAD_SQUARE, "square" },
        { PAD_TRIANGLE, "triangle" }, { PAD_L1, "l1" }, { PAD_R1, "r1" }, { PAD_L2, "l2" },
        { PAD_R2, "r2" }, { PAD_L3, "l3" }, { PAD_R3, "r3" }, { PAD_UP, "up" },
        { PAD_DOWN, "down" }, { PAD_LEFT, "left" }, { PAD_RIGHT, "right" },
        { PAD_OPTIONS, "options" }, { PAD_CREATE, "create" }, { PAD_PS, "ps" },
        { PAD_TOUCHPAD, "touchpad" },
    };
    host_t *h = *w->cfg.host;
    char out[8192], a[18];
    size_t n = 0;
    int i, first = 1;

#define PUT(...) do { int k_ = snprintf(out + n, sizeof out - n, __VA_ARGS__); \
                      if (k_ > 0) n += (size_t)k_ < sizeof out - n ? (size_t)k_ : sizeof out - n - 1; } while (0)

    {
        static const char *const bt[] = { "trying", "ok", "failed" };
        char reason[200], combo[100], rkey[16];
        uint32_t pb = 0;
        int perr = 0, readable = w->cfg.physical ? w->cfg.physical(&pb, &perr) : 0;
        int st = *w->cfg.bt_state;
        size_t b;
        int fb = 1;

        json_text(reason, sizeof reason, w->cfg.bt_reason);
        json_text(combo, sizeof combo, w->cfg.combo_text);
        json_text(rkey, sizeof rkey, w->cfg.bt_reason_key ? *w->cfg.bt_reason_key : "");
        PUT("{\"version\":\"%s\",\"running\":%s,\"pairing\":%d,\"bluetooth\":\"%s\","
            "\"reason\":\"%s\",\"reasonKey\":\"%s\",\"lang\":\"%s\",\"combo\":\"%s\","
            "\"physical\":{\"readable\":%s,\"error\":\"%08x\",\"buttons\":[",
            w->cfg.version, h ? "true" : "false", h ? host_pairing_left(h) : 0,
            bt[st >= 0 && st <= 2 ? st : 0], reason, rkey, i18n_code(i18n_get()), combo,
            readable ? "true" : "false", (unsigned)perr);
        for (b = 0; readable && b < sizeof names / sizeof names[0]; b++)
            if (pb & names[b].bit) {
                PUT("%s\"%s\"", fb ? "" : ",", names[b].name);
                fb = 0;
            }
        PUT("]},\"pads\":[");
    }
    for (i = 0; h && i < HOST_MAX_PADS; i++) {
        pad_info info;
        pad_state st;
        size_t b;
        int fb = 1;
        if (!host_pad_get(h, i, &info, &st)) continue;
        fmt_addr(a, info.addr);
        PUT("%s{\"slot\":%d,\"transport\":\"bt\",\"profile\":\"%s\",\"model\":\"%s\",\"vid\":\"%04x\",\"pid\":\"%04x\",\"addr\":\"%s\",\"virtual\":\"%s\","
            "\"battery\":%d,\"charging\":%s,\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"l2\":%u,\"r2\":%u,"
            "\"buttons\":[",
            first ? "" : ",", i + 1, info.profile,
            pad_display_name(info.vid, info.pid, info.profile), info.vid, info.pid, a, virtual_name(w, i),
            st.has_battery ? st.battery_pct : -1, st.charging ? "true" : "false",
            st.lx, st.ly, st.rx, st.ry, st.l2, st.r2);
        for (b = 0; b < sizeof names / sizeof names[0]; b++)
            if (st.buttons & names[b].bit) {
                PUT("%s\"%s\"", fb ? "" : ",", names[b].name);
                fb = 0;
            }
        PUT("]}");
        first = 0;
    }
    for (i = 0; w->cfg.wired_snapshot && i < WIRED_MAX_PADS; i++) {
        wired_pad_snapshot p;
        char model[96], path[48];
        size_t b;
        int fb = 1;
        if (!w->cfg.wired_snapshot(i, &p)) continue;
        json_text(model, sizeof model, p.name);
        json_text(path, sizeof path, p.path);
        PUT("%s{\"slot\":%d,\"transport\":\"usb\",\"profile\":\"USB\",\"model\":\"%s\",\"vid\":\"%04x\",\"pid\":\"%04x\",\"addr\":\"%s\",\"virtual\":\"%s\","
            "\"battery\":-1,\"charging\":false,\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"l2\":%u,\"r2\":%u,\"buttons\":[",
            first ? "" : ",", p.slot + 1, model, p.vid, p.pid, path, virtual_name(w, p.slot),
            p.state.lx, p.state.ly, p.state.rx, p.state.ry, p.state.l2, p.state.r2);
        for (b = 0; b < sizeof names / sizeof names[0]; b++)
            if (p.state.buttons & names[b].bit) {
                PUT("%s\"%s\"", fb ? "" : ",", names[b].name);
                fb = 0;
            }
        PUT("]}");
        first = 0;
    }
    PUT("],\"paired\":[");
    first = 1;
    for (i = 0; h && i < host_paired_count(h); i++) {
        host_paired_info p;
        if (!host_paired_get(h, i, &p)) break;
        fmt_addr(a, p.addr);
        PUT("%s{\"addr\":\"%s\",\"vid\":\"%04x\",\"pid\":\"%04x\",\"le\":%s,\"connected\":%s}",
            first ? "" : ",", a, p.vid, p.pid, p.le ? "true" : "false",
            p.connected ? "true" : "false");
        first = 0;
    }
    PUT("]}");
#undef PUT
    respond(fd, 200, "application/json", out, n);
}

/* The end of the log, as plain text with anything unprintable dropped. */
static void api_log(web_t *w, int fd)
{
    char buf[LOG_TAIL];
    FILE *f = fopen(w->cfg.log_path, "rb");
    size_t n = 0, i;
    long size;

    if (!f) {
        respond(fd, 200, "text/plain; charset=utf-8", "", 0);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, size > LOG_TAIL ? size - LOG_TAIL : 0, SEEK_SET);
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    for (i = 0; i < n; i++)
        if ((unsigned char)buf[i] < 0x20 && buf[i] != '\n') buf[i] = ' ';
    respond(fd, 200, "text/plain; charset=utf-8", buf, n);
}

/* The whole log (up to 192 KB, from its start): for reports. */
static void api_log_full(web_t *w, int fd)
{
    static char buf[192 * 1024];
    FILE *f = fopen(w->cfg.log_path, "rb");
    size_t n = 0, i;

    (void)w;
    if (f) {                            /* the last 192 KB: the newest lines */
        long size;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, size > (long)sizeof buf ? size - (long)sizeof buf : 0, SEEK_SET);
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
    }
    for (i = 0; i < n; i++)
        if ((unsigned char)buf[i] < 0x20 && buf[i] != '\n') buf[i] = ' ';
    respond(fd, 200, "text/plain; charset=utf-8", buf, n);
}

/* "VVVV_PPPP" (hex ids, either case) and nothing after. */
static int parse_dev(const char *s, uint16_t *vid, uint16_t *pid)
{
    unsigned v = 0, p = 0;
    int i;

    for (i = 0; i < 9; i++) {
        int c = (unsigned char)s[i], d;
        if (i == 4) {
            if (c != '_') return 0;
            continue;
        }
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        if (i < 4) v = v << 4 | (unsigned)d;
        else p = p << 4 | (unsigned)d;
    }
    if (s[9] != '\0') return 0;
    *vid = (uint16_t)v;
    *pid = (uint16_t)p;
    return 1;
}

/* Every controller model seen or with a map, and its map in full:
 *   {"devices":[{"dev":"045E_0B13","vid":"045e","pid":"0b13","name":"...",
 *     "seen":true,"saved":false,"map":{"cross":"cross",...,"p1":"none",...},
 *     "swap_sticks":false,"invert_ly":false,"invert_ry":false,
 *     "swap_triggers":false,"hair_l2":false,"hair_r2":false}]} */
static void api_remap_list(int fd)
{
    static char out[20000];
    size_t n = 0;
    remap_info r;
    int i, s;

#define PUT(...) do { int k_ = snprintf(out + n, sizeof out - n, __VA_ARGS__); \
                      if (k_ > 0) n += (size_t)k_ < sizeof out - n ? (size_t)k_ : sizeof out - n - 1; } while (0)
#define TF(x) ((x) ? "true" : "false")
    PUT("{\"devices\":[");
    for (i = 0; remap_get(i, &r); i++) {
        char name[REMAP_NAME_MAX];
        json_text(name, sizeof name, r.name);
        PUT("%s{\"dev\":\"%04X_%04X\",\"vid\":\"%04x\",\"pid\":\"%04x\",\"name\":\"%s\","
            "\"seen\":%s,\"saved\":%s,\"map\":{",
            i ? "," : "", r.vid, r.pid, r.vid, r.pid, name, TF(r.seen), TF(r.saved));
        for (s = 0; s < REMAP_SRC_N; s++) {
            uint8_t t = r.map.target[s];
            PUT("%s\"%s\":\"%s\"", s ? "," : "", remap_source_name(s),
                t < REMAP_BTN_N ? remap_source_name(t) : "none");
        }
        PUT("},\"swap_sticks\":%s,\"invert_ly\":%s,\"invert_ry\":%s,\"swap_triggers\":%s,"
            "\"hair_l2\":%s,\"hair_r2\":%s}",
            TF(r.map.swap_sticks), TF(r.map.invert_ly), TF(r.map.invert_ry),
            TF(r.map.swap_triggers), TF(r.map.hair_l2), TF(r.map.hair_r2));
    }
    PUT("]}");
#undef TF
#undef PUT
    respond(fd, 200, "application/json", out, n);
}

/* POST /api/remap?dev=VVVV_PPPP, the map as file text in the body. */
static void api_remap_set(int fd, const char *dev, const char *body)
{
    uint16_t vid, pid;
    const char *err = "";
    int line = 0;
    char msg[80];

    if (!parse_dev(dev, &vid, &pid)) {
        respond_text(fd, 400, "{\"error\":\"device\"}");
        return;
    }
    if (strlen(body) > REMAP_TEXT_MAX) {
        respond_text(fd, 400, "{\"error\":\"too large\"}");
        return;
    }
    if (remap_set(vid, pid, body, &err, &line)) {
        respond_text(fd, 200, "{\"ok\":true}");
        return;
    }
    if (strcmp(err, "syntax") == 0) {
        snprintf(msg, sizeof msg, "{\"error\":\"syntax\",\"line\":%d}", line);
        respond_text(fd, 400, msg);
    } else {
        snprintf(msg, sizeof msg, "{\"error\":\"%s\"}", strcmp(err, "full") == 0 ? "full" : "write");
        respond_text(fd, 500, msg);
    }
}

/* ---- Bloom Core intents (POST /hub/intent) ---------------------------------- */

/* {"hub_api":1,"intent":"...","args":{...},"from":"..."} from Bloom Core,
 * which has already checked the caller. We check again: the peer must be
 * this console and the X-Hub-Token header must match /data/hub/token. */
static void hub_intent(web_t *w, int fd, const char *req, int loopback)
{
    const char *body = strstr(req, "\r\n\r\n");
    char token[40], name[48], text[REMAP_TEXT_MAX + 1], dev[16], msg[160];
    size_t len;
    const char *v;

    if (!loopback || !w->cfg.token_path ||
        !header(req, "X-Hub-Token", token, sizeof token) || !hubcard_token_ok(w->cfg.token_path, token)) {
        log_line("hub: intent refused (%s)", !loopback ? "not from this console" : "token");
        respond_text(fd, 403, "{\"error\":\"forbidden\"}");
        return;
    }
    body = body ? body + 4 : "";
    len = strlen(body);
    if (!hubcard_intent_name(body, len, name, sizeof name)) {
        respond_text(fd, 400, "{\"error\":\"bad intent\"}");
        return;
    }
    if (strcmp(name, "overlay.show") == 0) {
        /* A payload cannot draw over the screen: we say where the overlay
         * lives and the caller (Omni) opens it. */
        snprintf(msg, sizeof msg, "{\"ok\":true,\"action\":\"open\",\"url\":\"http://127.0.0.1:%d/overlay\"}", w->cfg.port);
        log_line("hub: intent overlay.show");
        respond_text(fd, 200, msg);
    } else if (strcmp(name, "input.remap.profile") == 0) {
        /* args: {"dev":"VVVV_PPPP","map":"<remap file text>"} or {"dev":...,"reset":true} */
        uint16_t vid, pid;
        const char *err = "";
        int line = 0;

        /* Named selection is additive; mixed named/inline requests are refused.
         * app is a namespace, not proof that an application is foreground. */
        if (hj_find(body,len,"args","app") || hj_find(body,len,"args","profile")) {
            char app[64], profile[64];
            const char *av=hj_find(body,len,"args","app");
            const char *pv=hj_find(body,len,"args","profile");
            if(hj_count(body,len,NULL,"args")!=1 || hj_count(body,len,"args","app")!=1 ||
               hj_count(body,len,"args","profile")!=1 || !av || !pv || !hj_string(av,app,sizeof app) || !hj_string(pv,profile,sizeof profile) ||
               hj_find(body,len,"args","dev") || hj_find(body,len,"args","map") || hj_find(body,len,"args","reset")) {
                respond_text(fd,400,"{\"error\":\"profile-args\"}");return;
            }
            if(!w->cfg.profiles_path){respond_text(fd,503,"{\"error\":\"profiles-unavailable\"}");return;}
            if(!remap_profile_load(w->cfg.profiles_path,app,profile,&vid,&pid,text,&err)) {
                snprintf(msg,sizeof msg,"{\"error\":\"%s\"}",err);
                respond_text(fd,strcmp(err,"missing")==0?404:400,msg);return;
            }
            if(!remap_set(vid,pid,text,&err,&line)) {
                snprintf(msg,sizeof msg,"{\"error\":\"%s\"}",err);
                respond_text(fd,strcmp(err,"syntax")==0?400:500,msg);return;
            }
            log_line("hub: named profile %s/%s applied to %04x:%04x",app,profile,vid,pid);
            respond_text(fd,200,"{\"ok\":true,\"scope\":\"controller-model\"}");return;
        }
        if (!(v = hj_find(body, len, "args", "dev")) || !hj_string(v, dev, sizeof dev) || !parse_dev(dev, &vid, &pid)) {
            respond_text(fd, 400, "{\"error\":\"device\"}");
        } else if (hj_true(hj_find(body, len, "args", "reset"))) {
            remap_reset(vid, pid);
            log_line("hub: intent input.remap.profile reset %s", dev);
            respond_text(fd, 200, "{\"ok\":true,\"reset\":true}");
        } else if (!(v = hj_find(body, len, "args", "map")) || !hj_string(v, text, sizeof text)) {
            respond_text(fd, 400, "{\"error\":\"map\"}");
        } else if (remap_set(vid, pid, text, &err, &line)) {
            log_line("hub: intent input.remap.profile applied to %s", dev);
            respond_text(fd, 200, "{\"ok\":true}");
        } else if (strcmp(err, "syntax") == 0) {
            snprintf(msg, sizeof msg, "{\"error\":\"syntax\",\"line\":%d}", line);
            respond_text(fd, 400, msg);
        } else {
            snprintf(msg, sizeof msg, "{\"error\":\"%s\"}", strcmp(err, "full") == 0 ? "full" : "write");
            respond_text(fd, 500, msg);
        }
    } else {
        respond_text(fd, 404, "{\"error\":\"unknown intent\"}");
    }
}

/* GET /tools[/...]: the 0.4.4 Controller tools pages, a fixed table built from web/tools/ (exact match only;
 * a query string is ignored). "/tools/info" redirects to "/tools/info/" so the pages' relative links work. */
static void serve_tool(int fd, const char *path)
{
    char clean[256], head[320];
    size_t k = 0;
    unsigned int i;

    while (path[k] && path[k] != '?' && path[k] != '#' && k + 1 < sizeof clean) {
        clean[k] = path[k];
        k++;
    }
    clean[k] = '\0';
    for (i = 0; i < web_tools_count; i++) {
        const web_tool_file *f = &web_tools_files[i];
        if (strcmp(f->url, clean) != 0) continue;
        if (f->redirect) {
            int n = snprintf(head, sizeof head,
                             "HTTP/1.1 301 Moved Permanently\r\nLocation: %s/\r\nContent-Length: 0\r\n"
                             "Cache-Control: no-store\r\nConnection: close\r\n\r\n", f->url);
            if (n > 0 && (size_t)n < sizeof head) send_all(fd, head, (size_t)n);
            return;
        }
        respond(fd, 200, f->mime, (const char *)f->data, f->len);
        return;
    }
    respond_text(fd, 404, "{\"error\":\"not found\"}");
}

/* Automatic pairing (the UI switch): on unless the no_auto_pair file exists.
 * GET /api/autopair -> {"enabled":true|false}
 * PUT /api/autopair {"enabled":true|false} (X-PadBridge: 1) removes or creates the file. */
static void api_autopair_get(web_t *w, int fd)
{
    if (!w->cfg.autopair_off_path) { respond_text(fd, 503, "{\"error\":\"unavailable\"}"); return; }
    respond_text(fd, 200, access(w->cfg.autopair_off_path, F_OK) == 0 ? "{\"enabled\":false}" : "{\"enabled\":true}");
}

static void api_autopair_put(web_t *w, int fd, const char *body)
{
    size_t len = strlen(body);
    const char *v;
    int on;

    if (!w->cfg.autopair_off_path) { respond_text(fd, 503, "{\"error\":\"unavailable\"}"); return; }
    if (!hj_valid(body, len) || hj_count(body, len, NULL, "enabled") != 1 || !(v = hj_find(body, len, NULL, "enabled"))) {
        respond_text(fd, 400, "{\"error\":\"enabled\"}");
        return;
    }
    if (hj_true(v)) on = 1;
    else if (strncmp(v, "false", 5) == 0) on = 0;
    else { respond_text(fd, 400, "{\"error\":\"enabled\"}"); return; }
    if (on) {
        if (unlink(w->cfg.autopair_off_path) != 0 && errno != ENOENT) {
            log_line("web: could not remove %s (errno %d)", w->cfg.autopair_off_path, errno);
            respond_text(fd, 500, "{\"error\":\"save\"}");
            return;
        }
    } else {
        int f = open(w->cfg.autopair_off_path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (f < 0) {
            log_line("web: could not create %s (errno %d)", w->cfg.autopair_off_path, errno);
            respond_text(fd, 500, "{\"error\":\"save\"}");
            return;
        }
        close(f);
    }
    log_line("web: automatic pairing %s", on ? "on" : "off");
    respond_text(fd, 200, on ? "{\"enabled\":true}" : "{\"enabled\":false}");
}

static void handle(web_t *w, int fd, char *req, int loopback)
{
    char method[8], path[256], hostname[64], custom[8];
    host_t *h = *w->cfg.host;

    if (sscanf(req, "%7s %255s", method, path) != 2) {
        respond_text(fd, 400, "{\"error\":\"bad request\"}");
        return;
    }
    if (!header(req, "Host", hostname, sizeof hostname) || !host_is_literal(hostname)) {
        respond_text(fd, 400, "{\"error\":\"host\"}");
        return;
    }
    if (strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        respond(fd, 200, "text/html; charset=utf-8", (const char *)web_page_html, web_page_html_len);
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/classic") == 0) {
        respond(fd, 200, "text/html; charset=utf-8", (const char *)web_classic_html, web_classic_html_len);
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/icon.png") == 0) {
        respond(fd, 200, "image/png", (const char *)icon_png_data, icon_png_data_len);
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/overlay") == 0) {
        respond(fd, 200, "text/html; charset=utf-8", (const char *)web_overlay_html, web_overlay_html_len);
        return;
    }
    if (strcmp(method, "GET") == 0 && (strcmp(path, "/tools") == 0 || strncmp(path, "/tools/", 7) == 0 || strncmp(path, "/tools?", 7) == 0)) {
        serve_tool(fd, path);
        return;
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/hub/intent") == 0) {
        hub_intent(w, fd, req, loopback);
        return;
    }
    if (strncmp(path, "/api/", 5) != 0) {
        respond_text(fd, 404, "{\"error\":\"not found\"}");
        return;
    }

    /* Anything that changes something must carry a header only this page
     * sends: a browser will not add it to a request made by another site
     * without asking us first, and we never agree. */
    if ((strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) &&
        (!header(req, "X-PadBridge", custom, sizeof custom) || strcmp(custom, "1") != 0)) {
        respond_text(fd, 403, "{\"error\":\"forbidden\"}");
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/health") == 0) {
        char msg[96], ver[32];
        json_text(ver, sizeof ver, w->cfg.version);
        snprintf(msg, sizeof msg, "{\"ok\":true,\"version\":\"%s\"}", ver);
        respond_text(fd, 200, msg);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/state") == 0) {
        api_state(w, fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/log") == 0) {
        api_log(w, fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/logfull") == 0) {
        api_log_full(w, fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/pair") == 0 && h) {
        host_pair(h, 60);
        log_line("web: pairing started");
        respond_text(fd, 200, "{\"ok\":true}");
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/forget?addr=", 17) == 0 && h) {
        unsigned char a[6];
        if (!parse_addr(path + 17, a)) respond_text(fd, 400, "{\"error\":\"address\"}");
        else if (!host_forget(h, a)) respond_text(fd, 404, "{\"error\":\"not paired\"}");
        else respond_text(fd, 200, "{\"ok\":true}");
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/ps?slot=", 13) == 0) {
        int n = atoi(path + 13);
        if (n < 1 || n > HOST_MAX_PADS + WIRED_MAX_PADS) respond_text(fd, 400, "{\"error\":\"slot\"}");
        else if (!w->cfg.press_ps || !w->cfg.press_ps(n - 1)) respond_text(fd, 404, "{\"error\":\"no pad\"}");
        else respond_text(fd, 200, "{\"ok\":true}");
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/rumble?slot=", 17) == 0 && h) {
        int n = atoi(path + 17);
        if (n < 1 || n > HOST_MAX_PADS) respond_text(fd, 400, "{\"error\":\"slot\"}");
        else if (!host_rumble_pulse(h, n - 1, 500)) respond_text(fd, 404, "{\"error\":\"no pad or no rumble\"}");
        else respond_text(fd, 200, "{\"ok\":true}");
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/retry") == 0) {
        if (*w->cfg.bt_state == WEB_BT_FAILED) {
            log_line("web: Bluetooth retry requested");
            *w->cfg.retry = 1;
            respond_text(fd, 200, "{\"ok\":true}");
        } else {
            respond_text(fd, 400, "{\"error\":\"not failed\"}");
        }
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/lang?code=", 15) == 0) {
        int lang = i18n_index(path + 15);
        if (lang < 0) {
            respond_text(fd, 400, "{\"error\":\"language\"}");
        } else {
            i18n_set(lang);
            if (w->cfg.lang_path && !i18n_save(w->cfg.lang_path))
                log_line("web: could not save the language in %s", w->cfg.lang_path);
            log_line("web: language %s", i18n_code(lang));
            respond_text(fd, 200, "{\"ok\":true}");
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/remap") == 0) {
        api_remap_list(fd);
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/remap?dev=", 15) == 0) {
        const char *body = strstr(req, "\r\n\r\n");
        api_remap_set(fd, path + 15, body ? body + 4 : "");
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/remap/reset?dev=", 21) == 0) {
        uint16_t vid, pid;
        if (!parse_dev(path + 21, &vid, &pid)) respond_text(fd, 400, "{\"error\":\"device\"}");
        else {
            remap_reset(vid, pid);
            respond_text(fd, 200, "{\"ok\":true}");
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/autopair") == 0) {
        api_autopair_get(w, fd);
    } else if (strcmp(method, "PUT") == 0 && strcmp(path, "/api/autopair") == 0) {
        const char *body = strstr(req, "\r\n\r\n");
        api_autopair_put(w, fd, body ? body + 4 : "");
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/stop") == 0) {
        log_line("web: stop requested");
        *w->cfg.stop = 1;
        respond_text(fd, 200, "{\"ok\":true}");
    } else {
        respond_text(fd, 404, "{\"error\":\"not found\"}");
    }
}

/* ---- serving ------------------------------------------------------------------- */

web_t *web_start(const web_cfg *cfg, int port)
{
    struct sockaddr_in sin;
    web_t *w;
    int one = 1, i;

    w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->cfg = *cfg;
    for (i = 0; i < WEB_CLIENTS; i++) w->c[i].fd = -1;

    w->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (w->fd < 0) {
        free(w);
        return NULL;
    }
    setsockopt(w->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(w->fd, (struct sockaddr *)&sin, sizeof sin) != 0 || listen(w->fd, 4) != 0) {
        log_line("web: port %d not available (errno %d), no web page", port, errno);
        close(w->fd);
        free(w);
        return NULL;
    }
    if (set_nonblock(w->fd) != 0) {     /* MC-02: a blocking listener would stall the controller loop in accept() */
        log_line("web: the listening socket can't be made non-blocking (errno %d): no web page", errno);
        close(w->fd);
        free(w);
        return NULL;
    }
    log_line("web: listening on port %d", port);
    return w;
}

/* Whether the request in buf is complete: the head, and the body its
 * Content-Length announces (cut there). 1: complete, 0: not yet, -1: the
 * body would not fit in REQ_MAX or its length is not a number. */
static int request_body(char *buf, int len)
{
    char cl[16], *end = strstr(buf, "\r\n\r\n"), *e;
    long want = 0;
    int head;

    if (!end) return 0;
    head = (int)(end - buf) + 4;
    if (header(buf, "Content-Length", cl, sizeof cl)) {
        want = strtol(cl, &e, 10);
        if (e == cl || *e != '\0' || want < 0 || want > REQ_MAX - head) return -1;
    }
    if (len < head + want) return 0;
    buf[head + want] = '\0';
    return 1;
}

void web_poll(web_t *w)
{
    long now = now_ms();
    int i;

    if (!w) return;
    for (;;) {                          /* new connections */
        struct sockaddr_in peer;
        socklen_t plen = sizeof peer;
        int fd = accept(w->fd, (struct sockaddr *)&peer, &plen), slot = -1;
        if (fd < 0) break;
        if (plen < sizeof peer || (!g_allow_any_peer && !peer_is_local(&peer))) {
            close(fd);                  /* not from the local network */
            continue;
        }
#ifdef SO_NOSIGPIPE
        {
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        }
#endif
        if (set_nonblock(fd) != 0) {    /* MC-02: never read a blocking client socket from the input loop */
            close(fd);
            continue;
        }
        for (i = 0; i < WEB_CLIENTS; i++) if (w->c[i].fd < 0) { slot = i; break; }
        if (slot < 0) {
            respond_text(fd, 429, "{\"error\":\"busy\"}");
            close(fd);
            continue;
        }
        w->c[slot].fd = fd;
        w->c[slot].loopback = peer_is_loopback(&peer);
        w->c[slot].len = 0;
        w->c[slot].t_start = now;
    }

    for (i = 0; i < WEB_CLIENTS; i++) {
        client *c = &w->c[i];
        ssize_t k;
        int body;

        if (c->fd < 0) continue;
        k = recv(c->fd, c->buf + c->len, (size_t)(REQ_MAX - c->len), 0);
        if (k > 0) c->len += (int)k;
        c->buf[c->len] = '\0';

        if ((body = request_body(c->buf, c->len)) > 0) {
            handle(w, c->fd, c->buf, c->loopback);
        } else if (body < 0) {
            respond_text(c->fd, 400, "{\"error\":\"too large\"}");
        } else if (k == 0 || (k < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            /* the client went away */
        } else if (c->len >= REQ_MAX) {
            respond_text(c->fd, 400, "{\"error\":\"too large\"}");
        } else if (now - c->t_start <= T_REQUEST) {
            continue;                   /* still coming */
        }
        close(c->fd);
        c->fd = -1;
    }
}

void web_stop(web_t *w)
{
    int i;

    if (!w) return;
    for (i = 0; i < WEB_CLIENTS; i++) if (w->c[i].fd >= 0) close(w->c[i].fd);
    close(w->fd);
    free(w);
}
