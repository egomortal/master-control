/* The web page's server against a host with a simulated pad connected. */
#include "sim.h"
#include "../src/host.h"
#include "../src/web.h"
#include "../src/ps5_vpad.h"
#include "../src/remap.h"
#include "../src/i18n.h"
#include "../src/util.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ifaddrs.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* A port per run, so two runs at once do not collide. */
static int g_port;
#define PORT g_port

static host_t *g_host;
static web_t *g_web;
static volatile int g_bt_state = WEB_BT_OK, g_retry;
static char g_reason[200] = "";
static int g_phys_readable = 1;

static int fake_physical(uint32_t *buttons, int *err)
{
    *err = g_phys_readable ? 0 : (int)0xFFFF0003;
    *buttons = PAD_L1 | PAD_R1;
    return g_phys_readable;
}

static int g_ps_calls, g_ps_slot = -1, g_ps_result = 1;

static int fake_press_ps(int slot)
{
    g_ps_calls++;
    g_ps_slot = slot;
    return g_ps_result;
}

static int g_wired_visible;
static int fake_virtual_status(int slot)
{
    return slot == HOST_MAX_PADS + 3 ? VPAD_STATUS_WAITING : VPAD_STATUS_READY;
}
static int fake_wired_snapshot(int index, wired_pad_snapshot *out)
{
    if (index < 0 || index >= g_wired_visible) return 0;
    memset(out, 0, sizeof *out);
    out->slot = HOST_MAX_PADS + index;
    out->vid = index == 0 ? 0x3537 : index == 1 ? 0x1532 : 0x045e;
    out->pid = index == 0 ? 0x1082 : index == 1 ? 0x0a14 : 0x0b12;
    snprintf(out->name, sizeof out->name, "%s", index == 0 ? "GameSir G7 SE (HID)" : index == 1 ? "Razer Wolverine Ultimate" : "Xbox USB");
    snprintf(out->path, sizeof out->path, "/dev/ugen2.%d", index + 2);
    out->state.lx = 128;
    out->state.ly = 128;
    out->state.rx = 128;
    out->state.ry = 128;
    out->state.buttons = index ? PAD_CIRCLE : PAD_CROSS;
    return 1;
}

static void run(int ms)
{
    long end = sim_now() + ms;
    while (sim_now() < end) {
        host_poll(g_host, 5);
        web_poll(g_web);
    }
}

/* Sends a raw request and serves it; returns the response (status line on). */
static int request(const char *raw, size_t rawlen, char *out, int max)
{
    struct sockaddr_in sin;
    int fd = socket(AF_INET, SOCK_STREAM, 0), n = 0, i;

    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&sin, sizeof sin) != 0) {
        close(fd);
        return -1;
    }
    if (rawlen) send(fd, raw, rawlen, 0);
    for (i = 0; i < 400; i++) {             /* the server runs in this thread */
        ssize_t k;
        web_poll(g_web);
        if (g_host) host_poll(g_host, 1);
        k = recv(fd, out + n, (size_t)(max - 1 - n), MSG_DONTWAIT);
        if (k > 0) n += (int)k;
        else if (k == 0) break;
        usleep(1000);
    }
    out[n] = '\0';
    close(fd);
    return n;
}

static int get(const char *method, const char *path, char *out, int max)
{
    char raw[512];
    int n = snprintf(raw, sizeof raw, "%s %s HTTP/1.1\r\nHost: 192.168.1.50:8766\r\nX-PadBridge: 1\r\n\r\n",
                     method, path);
    return request(raw, (size_t)n, out, max);
}

static int get_with(const char *head, char *out, int max)
{
    return request(head, strlen(head), out, max);
}

/* A request in two pieces, the server polled in between (a body that
 * arrives after its head). */
static int request_split(const char *a, const char *b, char *out, int max)
{
    struct sockaddr_in sin;
    int fd = socket(AF_INET, SOCK_STREAM, 0), n = 0, i;

    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&sin, sizeof sin) != 0) {
        close(fd);
        return -1;
    }
    send(fd, a, strlen(a), 0);
    for (i = 0; i < 50; i++) {
        web_poll(g_web);
        usleep(1000);
    }
    send(fd, b, strlen(b), 0);
    for (i = 0; i < 400; i++) {
        ssize_t k;
        web_poll(g_web);
        k = recv(fd, out + n, (size_t)(max - 1 - n), MSG_DONTWAIT);
        if (k > 0) n += (int)k;
        else if (k == 0) break;
        usleep(1000);
    }
    out[n] = '\0';
    close(fd);
    return n;
}

static int post_body(const char *path, const char *body, char *out, int max)
{
    char raw[2048];
    int n = snprintf(raw, sizeof raw, "POST %s HTTP/1.1\r\nHost: 192.168.1.50:8766\r\nX-PadBridge: 1\r\n"
                     "Content-Length: %u\r\n\r\n%s", path, (unsigned)strlen(body), body);
    return request(raw, (size_t)n, out, max);
}

/* POST /hub/intent as Bloom Core sends it (no X-PadBridge), optionally with a token. */
static int hub_post(const char *token, const char *body, char *out, int max)
{
    char raw[2048];
    int n = snprintf(raw, sizeof raw, "POST /hub/intent HTTP/1.0\r\nHost: 127.0.0.1:%d\r\n%s%s%s"
                     "Content-Type: application/json\r\nContent-Length: %u\r\n\r\n%s",
                     PORT, token ? "X-Hub-Token: " : "", token ? token : "", token ? "\r\n" : "",
                     (unsigned)strlen(body), body);
    return request(raw, (size_t)n, out, max);
}

/* The same, but from this machine's first non-loopback IPv4 address (as a
 * device on the network would come in). Returns -2 if there is none. */
static int hub_post_from_lan(const char *token, const char *body, char *out, int max)
{
    struct ifaddrs *ifa, *i;
    struct sockaddr_in sin;
    char raw[2048];
    int fd, n = 0, k, found = 0;

    if (getifaddrs(&ifa) != 0) return -2;
    for (i = ifa; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !(i->ifa_flags & IFF_LOOPBACK) &&
            (i->ifa_flags & IFF_UP)) {
            memcpy(&sin, i->ifa_addr, sizeof sin);
            found = 1;
            break;
        }
    freeifaddrs(ifa);
    if (!found) return -2;
    sin.sin_port = htons(PORT);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&sin, sizeof sin) != 0) { close(fd); return -2; }
    n = snprintf(raw, sizeof raw, "POST /hub/intent HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nX-Hub-Token: %s\r\n"
                 "Content-Length: %u\r\n\r\n%s", PORT, token, (unsigned)strlen(body), body);
    send(fd, raw, (size_t)n, 0);
    n = 0;
    for (k = 0; k < 400; k++) {
        ssize_t r;
        web_poll(g_web);
        r = recv(fd, out + n, (size_t)(max - 1 - n), MSG_DONTWAIT);
        if (r > 0) n += (int)r;
        else if (r == 0) break;
        usleep(1000);
    }
    out[n] = '\0';
    close(fd);
    return n;
}

int main(void)
{
    static const unsigned char addr[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    static char out[70000];
    static char icon_out[200000];
    volatile int stop = 0;
    sim_pad pad;
    sim_t *sim;
    web_cfg wc;
    int i;

    signal(SIGPIPE, SIG_IGN);
    g_port = 20000 + (int)(getpid() % 20000);
    unlink("build/test_web.db");
    memset(&pad, 0, sizeof pad);
    pad.mode = SIM_WAIT_PAIRING;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    pad.vid = 0x054C;
    pad.pid = 0x05C4;
    memset(pad.report, 0, 78);
    pad.report[0] = 0x11;
    pad.report[3] = 0x80; pad.report[4] = 0x80; pad.report[5] = 0x80; pad.report[6] = 0x80;
    pad.report[7] = 0x20 | 0x08;            /* cross, hat centred */
    pad.report[32] = 0x08;
    pad.report_len = 78;
    pad.needs_output = 1;
    sim = sim_new(&pad);
    g_host = host_open(sim_hci(sim), "build/test_web.db", NULL);
    memset(&wc, 0, sizeof wc);
    wc.host = &g_host;
    wc.stop = &stop;
    wc.retry = &g_retry;
    wc.bt_state = &g_bt_state;
    wc.bt_reason = g_reason;
    wc.log_path = "build/test_web.log";
    wc.version = "test";
    wc.combo_text = "L2 + R2 + OPTIONS";
    wc.lang_path = "build/test_web_language";
    wc.physical = fake_physical;
    wc.press_ps = fake_press_ps;
    wc.wired_snapshot = fake_wired_snapshot;
    wc.virtual_status = fake_virtual_status;
    wc.port = PORT;
    wc.token_path = "build/test_web_token";
    {
        FILE *t = fopen("build/test_web_token", "w");
        if (t) { fputs("00112233445566778899aabbccddeeff\n", t); fclose(t); }
    }
    g_web = web_start(&wc, PORT);
    CHECK(g_host && g_web);
    if (!g_host || !g_web) return 1;
    {
        FILE *f = fopen("build/test_web.log", "w");
        if (f) { fputs("line one\nline two\n", f); fclose(f); }
    }

    printf("the page\n");
    get("GET", "/", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 200", 12) == 0);
    CHECK(strstr(out, "<title>Master Control</title>") != NULL);
    get("GET", "/classic", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 200", 12) == 0);
    CHECK(strstr(out, "<title>EeveeLution PS5</title>") != NULL);
    CHECK(strstr(out, "src=\"/icon.png\"") != NULL);
    get("GET", "/icon.png", icon_out, sizeof icon_out);
    CHECK(strstr(icon_out, "Content-Type: image/png") != NULL);

    printf("pairing from the page, then the pad in the state\n");
    get("POST", "/api/pair", out, sizeof out);
    CHECK(strstr(out, "{\"ok\":true}") != NULL);
    run(8000);
    get("GET", "/api/state", out, sizeof out);
    CHECK(strstr(out, "\"profile\":\"DualShock 4\"") != NULL);
    CHECK(strstr(out, "\"addr\":\"06:05:04:03:02:01\"") != NULL);
    CHECK(strstr(out, "\"buttons\":[\"cross\"]") != NULL);
    CHECK(strstr(out, "\"battery\":100") != NULL);
    CHECK(strstr(out, "\"connected\":true") != NULL);

    CHECK(strstr(out, "\"bluetooth\":\"ok\"") != NULL);
    CHECK(strstr(out, "\"combo\":\"L2 + R2 + OPTIONS\"") != NULL);
    CHECK(strstr(out, "\"physical\":{\"readable\":true,\"error\":\"00000000\",\"buttons\":[\"l1\",\"r1\"]}") != NULL);

    printf("the user's pad not readable (the system has the input)\n");
    g_phys_readable = 0;
    get("GET", "/api/state", out, sizeof out);
    CHECK(strstr(out, "\"readable\":false,\"error\":\"ffff0003\",\"buttons\":[]") != NULL);
    g_phys_readable = 1;

    printf("no Bluetooth: the page still answers, says why, and can retry\n");
    {
        host_t *saved = g_host;
        g_host = NULL;
        g_bt_state = WEB_BT_FAILED;
        snprintf(g_reason, sizeof g_reason, "no reply \"0x1005\" \\ after 5 tries\n");
        get("GET", "/api/state", out, sizeof out);
        CHECK(strncmp(out, "HTTP/1.1 200", 12) == 0);
        CHECK(strstr(out, "\"running\":false") != NULL && strstr(out, "\"bluetooth\":\"failed\"") != NULL);
        CHECK(strstr(out, "\"reason\":\"no reply 0x1005  after 5 tries\"") != NULL);   /* nothing that breaks JSON */
        CHECK(strstr(out, "\"pads\":[]") != NULL && strstr(out, "\"paired\":[]") != NULL);
        get("POST", "/api/pair", out, sizeof out);                  /* nothing to pair with */
        CHECK(strncmp(out, "HTTP/1.1 404", 12) == 0);
        CHECK(g_retry == 0);
        get("POST", "/api/retry", out, sizeof out);
        CHECK(strstr(out, "{\"ok\":true}") != NULL && g_retry == 1);
        g_retry = 0;
        g_bt_state = WEB_BT_OK;
        get("POST", "/api/retry", out, sizeof out);                 /* not failed: refused */
        CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0 && g_retry == 0);
        g_host = saved;
    }

    printf("the log tail\n");
    get("GET", "/api/log", out, sizeof out);
    CHECK(strstr(out, "line two") != NULL);

    printf("the PS button of a pad\n");
    get("POST", "/api/ps?slot=1", out, sizeof out);
    CHECK(strstr(out, "{\"ok\":true}") != NULL && g_ps_calls == 1 && g_ps_slot == 0);   /* slots are 1-based in the page */
    get("POST", "/api/ps?slot=9", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0 && g_ps_calls == 1);
    get("POST", "/api/ps?slot=0", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0 && g_ps_calls == 1);
    g_ps_result = 0;                                    /* no live virtual pad there */
    get("POST", "/api/ps?slot=2", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 404", 12) == 0 && g_ps_calls == 2);
    g_ps_result = 1;
    get_with("POST /api/ps?slot=1 HTTP/1.1\r\nHost: 10.0.0.1\r\n\r\n", out, sizeof out);   /* no X-PadBridge header */
    CHECK(strncmp(out, "HTTP/1.1 403", 12) == 0 && g_ps_calls == 2);

    printf("wired pads appear beside Bluetooth pads with independent slots\n");
    g_wired_visible = 2;
    get("GET", "/api/state", out, sizeof out);
    CHECK(strstr(out, "\"slot\":5,\"transport\":\"usb\",\"profile\":\"USB\",\"model\":\"GameSir G7 SE (HID)\"") != NULL);
    CHECK(strstr(out, "\"slot\":6,\"transport\":\"usb\",\"profile\":\"USB\",\"model\":\"Razer Wolverine Ultimate\"") != NULL);
    CHECK(strstr(out, "\"addr\":\"/dev/ugen2.2\"") != NULL);
    g_wired_visible = 4;
    get("GET", "/api/state", out, sizeof out);
    CHECK(strstr(out, "\"slot\":8,\"transport\":\"usb\"") != NULL);
    CHECK(strstr(out, "\"virtual\":\"waiting\"") != NULL);
    get("POST", "/api/ps?slot=5", out, sizeof out);
    CHECK(strstr(out, "{\"ok\":true}") != NULL && g_ps_slot == HOST_MAX_PADS && g_ps_calls == 3);
    g_wired_visible = 0;

    printf("the language: English by default, set and saved through the API\n");
    unlink("build/test_web_language");
    get("GET", "/api/state", out, sizeof out);
    CHECK(strstr(out, "\"lang\":\"en\"") != NULL && strstr(out, "\"reasonKey\":\"\"") != NULL);
    get("POST", "/api/lang?code=ar", out, sizeof out);
    CHECK(strstr(out, "{\"ok\":true}") != NULL && i18n_get() == i18n_index("ar"));
    get("GET", "/api/state", out, sizeof out);
    CHECK(strstr(out, "\"lang\":\"ar\"") != NULL);
    i18n_set(0);
    CHECK(i18n_load("build/test_web_language") == i18n_index("ar"));
    get("POST", "/api/lang?code=xx", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0 && i18n_get() == i18n_index("ar"));
    get_with("POST /api/lang?code=en HTTP/1.1\r\nHost: 10.0.0.1\r\n\r\n", out, sizeof out);   /* no header */
    CHECK(strncmp(out, "HTTP/1.1 403", 12) == 0 && i18n_get() == i18n_index("ar"));
    get("POST", "/api/lang?code=en", out, sizeof out);
    CHECK(i18n_get() == 0);
    unlink("build/test_web_language");

    printf("forgetting the pad\n");
    get("POST", "/api/forget?addr=zz", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0);
    get("POST", "/api/forget?addr=06:05:04:03:02:01", out, sizeof out);
    CHECK(strstr(out, "{\"ok\":true}") != NULL);
    run(300);
    CHECK(host_paired_count(g_host) == 0);
    get("GET", "/api/state", out, sizeof out);
    CHECK(strstr(out, "\"pads\":[]") != NULL && strstr(out, "\"paired\":[]") != NULL);

    printf("bad requests: unknown, garbage, oversized, silent, many at once\n");
    get("GET", "/nothing", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 404", 12) == 0);
    {
        static const char junk[] = "\x01\x02\x03 garbage\r\n\r\n";
        request(junk, sizeof junk - 1, out, sizeof out);
    }
    CHECK(strncmp(out, "HTTP/1.1 4", 10) == 0);
    {
        static char big[10000];
        memset(big, 'A', sizeof big);
        request(big, sizeof big, out, sizeof out);
        CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0);
    }
    for (i = 0; i < 10; i++) request("GET / HTTP/1.1\r\n", 16, out, sizeof out);  /* never finished */
    get("GET", "/", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 200", 12) == 0);         /* still serving */

    printf("defences that need no PIN\n");
    /* a POST without the page's own header, as a forged cross-site form would send */
    get_with("POST /api/stop HTTP/1.1\r\nHost: 192.168.1.50:8766\r\n\r\n", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 403", 12) == 0 && stop == 0);
    /* DNS rebinding: the browser's Host is the attacker's name */
    get_with("GET /api/state HTTP/1.1\r\nHost: evil.example.com\r\nX-PadBridge: 1\r\n\r\n", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0);
    get_with("GET / HTTP/1.1\r\n\r\n", out, sizeof out);          /* no Host at all */
    CHECK(strncmp(out, "HTTP/1.1 400", 12) == 0);
    get_with("OPTIONS /api/stop HTTP/1.1\r\nHost: 192.168.1.50\r\nOrigin: http://evil.example.com\r\n"
             "Access-Control-Request-Method: POST\r\n\r\n", out, sizeof out);
    CHECK(strstr(out, "Access-Control-Allow") == NULL);     /* a preflight is never agreed to */
    CHECK(stop == 0);

    printf("random and mutated requests (under the sanitizers when run with make fuzz)\n");
    {
        static const char *seeds[] = {
            "GET /api/state HTTP/1.1\r\nHost: 10.0.0.1\r\nX-PadBridge: 1\r\n\r\n",
            "POST /api/forget?addr=06:05:04:03:02:01 HTTP/1.1\r\nHost: 10.0.0.1\r\nX-PadBridge: 1\r\n\r\n",
            "GET /api/log HTTP/1.1\r\nHost: localhost:8766\r\n\r\n",
            "POST /api/remap?dev=045E_0B13 HTTP/1.1\r\nHost: 10.0.0.1\r\nX-PadBridge: 1\r\n"
            "Content-Length: 31\r\n\r\ncross = circle\nswap_sticks = 1\n",
        };
        unsigned seed = 99;
        int round;
        for (round = 0; round < 600; round++) {
            char req[400];
            size_t n;
            int k, m;
            seed = seed * 1103515245u + 12345u;
            snprintf(req, sizeof req, "%s", seeds[(seed >> 8) % 4]);
            n = strlen(req);
            m = (int)((seed >> 12) % 6);
            for (k = 0; k < m; k++) {
                seed = seed * 1103515245u + 12345u;
                req[(seed >> 8) % n] = (char)(seed >> 20);
            }
            if (round % 5 == 0) n = (seed >> 6) % n;            /* truncated */
            request(req, n, out, sizeof out);
        }
        get("GET", "/api/state", out, sizeof out);
        CHECK(strncmp(out, "HTTP/1.1 200", 12) == 0);           /* still alive and well */
    }

    printf("remapping: list, save, refuse, reset\n");
    remap_init(NULL);                       /* nothing on disk in this test */
    get("GET", "/api/remap", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 200") == out && strstr(out, "{\"devices\":[]}") != NULL);
    remap_note_device(0x045E, 0x0B13, "Xbox \"Wireless\" Controller");
    get("GET", "/api/remap", out, sizeof out);
    CHECK(strstr(out, "\"dev\":\"045E_0B13\",\"vid\":\"045e\",\"pid\":\"0b13\","
                      "\"name\":\"Xbox Wireless Controller\",\"seen\":true,\"saved\":false") != NULL);
    CHECK(strstr(out, "\"map\":{\"cross\":\"cross\",") != NULL && strstr(out, "\"m6\":\"none\"}") != NULL);
    post_body("/api/remap?dev=045E_0B13", "cross = circle\np1 = triangle\nhair_r2 = 1\n", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 200") == out && strstr(out, "{\"ok\":true}") != NULL);
    {
        pad_state ps;
        pad_state_reset(&ps);
        ps.buttons = PAD_CROSS;
        ps.extra = PAD_X_P1;
        remap_apply(0x045E, 0x0B13, &ps);
        CHECK(ps.buttons == (PAD_CIRCLE | PAD_TRIANGLE));
    }
    get("GET", "/api/remap", out, sizeof out);
    CHECK(strstr(out, "\"saved\":true,\"map\":{\"cross\":\"circle\",") != NULL);
    CHECK(strstr(out, "\"p1\":\"triangle\"") != NULL && strstr(out, "\"hair_r2\":true}") != NULL);
    post_body("/api/remap?dev=045E_0B13", "cross = circle\nnonsense\n", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out && strstr(out, "{\"error\":\"syntax\",\"line\":2}") != NULL);
    post_body("/api/remap?dev=045E_0B1", "cross = circle\n", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out && strstr(out, "device") != NULL);
    post_body("/api/remap?dev=045E_0B13x", "cross = circle\n", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out);
    get_with("POST /api/remap?dev=045E_0B13 HTTP/1.1\r\nHost: 192.168.1.50:8766\r\nContent-Length: 4\r\n\r\nl1=x", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 403") == out);
    get_with("POST /api/remap?dev=045E_0B13 HTTP/1.1\r\nHost: 192.168.1.50:8766\r\nX-PadBridge: 1\r\n"
             "Content-Length: 9000\r\n\r\nl1=r1", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out && strstr(out, "too large") != NULL);
    get_with("POST /api/remap?dev=045E_0B13 HTTP/1.1\r\nHost: 192.168.1.50:8766\r\nX-PadBridge: 1\r\n"
             "Content-Length: -1\r\n\r\n", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out);
    printf("a body that comes after its head\n");
    request_split("POST /api/remap?dev=045E_0B13 HTTP/1.1\r\nHost: 192.168.1.50:8766\r\nX-PadBridge: 1\r\n"
                  "Content-Length: 14\r\n\r\ncross ", "= square\nGARBAGE", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 200") == out);
    {
        pad_state ps;
        pad_state_reset(&ps);
        ps.buttons = PAD_CROSS;
        remap_apply(0x045E, 0x0B13, &ps);
        CHECK(ps.buttons == PAD_SQUARE);
    }
    get("POST", "/api/remap/reset?dev=045e_0b13", out, sizeof out);
    CHECK(strstr(out, "{\"ok\":true}") != NULL);
    get("GET", "/api/remap", out, sizeof out);
    CHECK(strstr(out, "\"saved\":false,\"map\":{\"cross\":\"cross\",") != NULL);
    get("POST", "/api/remap/reset?dev=zz", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out);

    printf("Omnibloom: health, the overlay, Bloom Core intents\n");
    get("GET", "/api/health", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 200", 12) == 0 && strstr(out, "{\"ok\":true,\"version\":\"test\"}") != NULL);
    get("GET", "/overlay", out, sizeof out);
    CHECK(strncmp(out, "HTTP/1.1 200", 12) == 0 && strstr(out, "<title>Master Control · Quick overlay</title>") != NULL &&
          strstr(out, "window.omniPadController") != NULL);
    hub_post(NULL, "{\"hub_api\":1,\"intent\":\"overlay.show\",\"args\":{}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 403") == out);                                       /* no token */
    hub_post("00112233445566778899aabbccddeefe", "{\"hub_api\":1,\"intent\":\"overlay.show\",\"args\":{}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 403") == out);                                       /* wrong token */
    get_with("POST /hub/intent HTTP/1.1\r\nHost: 127.0.0.1\r\nX-PadBridge: 1\r\nContent-Length: 2\r\n\r\n{}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 403") == out);                                       /* the page's header is not a token */
    hub_post("00112233445566778899aabbccddeeff", "{\"hub_api\":1,\"intent\":\"overlay.show\",\"args\":{},\"from\":\"omni\"}", out, sizeof out);
    {
        char want[96];
        snprintf(want, sizeof want, "\"url\":\"http://127.0.0.1:%d/overlay\"", PORT);
        CHECK(strstr(out, "HTTP/1.1 200") == out && strstr(out, "\"action\":\"open\"") && strstr(out, want));
    }
    hub_post("00112233445566778899aabbccddeeff",
             "{\"hub_api\":1,\"intent\":\"input.remap.profile\",\"args\":{\"dev\":\"045E_0B13\",\"map\":\"cross = square\\nl1 = r1\\n\"}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 200") == out && strstr(out, "{\"ok\":true}"));
    get("GET", "/api/remap", out, sizeof out);
    CHECK(strstr(out, "\"cross\":\"square\"") != NULL);
    hub_post("00112233445566778899aabbccddeeff",
             "{\"hub_api\":1,\"intent\":\"input.remap.profile\",\"args\":{\"dev\":\"045E_0B13\",\"map\":\"cross = \\nnonsense\\n\"}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out && strstr(out, "syntax"));
    hub_post("00112233445566778899aabbccddeeff",
             "{\"hub_api\":1,\"intent\":\"input.remap.profile\",\"args\":{\"dev\":\"045E_0B13\",\"reset\":true}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 200") == out && strstr(out, "\"reset\":true"));
    get("GET", "/api/remap", out, sizeof out);
    CHECK(strstr(out, "\"cross\":\"square\"") == NULL);
    hub_post("00112233445566778899aabbccddeeff",
             "{\"hub_api\":1,\"intent\":\"input.remap.profile\",\"args\":{\"dev\":\"zz\",\"map\":\"\"}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out && strstr(out, "device"));
    hub_post("00112233445566778899aabbccddeeff",
             "{\"hub_api\":1,\"intent\":\"input.remap.profile\",\"args\":{\"dev\":\"045E_0B13\"}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out && strstr(out, "map"));
    hub_post("00112233445566778899aabbccddeeff", "{\"hub_api\":1,\"intent\":\"files.pick\",\"args\":{}}", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 404") == out);                                       /* not ours */
    hub_post("00112233445566778899aabbccddeeff", "{\"hub_api\":1,\"intent\":", out, sizeof out);
    CHECK(strstr(out, "HTTP/1.1 400") == out);                                       /* broken JSON */
    {
        int r = hub_post_from_lan("00112233445566778899aabbccddeeff", "{\"hub_api\":1,\"intent\":\"overlay.show\"}", out, sizeof out);
        if (r == -2) printf("  (no non-loopback address here: the network check is skipped)\n");
        /* right token, not from the console: a private-network peer gets 403, any
         * other is dropped before a request is read (empty answer). Never served. */
        else CHECK(strstr(out, "HTTP/1.1 200") == NULL && (out[0] == '\0' || strstr(out, "HTTP/1.1 403") == out));
    }

    printf("stop from the page\n");
    get("POST", "/api/stop", out, sizeof out);
    CHECK(stop == 1);

    web_stop(g_web);
    host_close(g_host);
    sim_free(sim);
    printf(g_fail ? "%d check(s) failed\n" : "all web checks passed\n", g_fail);
    return g_fail != 0;
}
