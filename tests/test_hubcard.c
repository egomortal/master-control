/* SPDX-License-Identifier: GPL-3.0-or-later
 * The Omnibloom plug-in standard on Master Control's side: the card, the
 * runtime file, the token, intents, and the small JSON reader under them. */
#include "../src/hubcard.h"
#include "../src/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static char g_root[] = "/tmp/mc-hubcard-XXXXXX";

static const char *at(const char *name)
{
    static char buf[4][300];
    static int k;
    k = (k + 1) & 3;
    snprintf(buf[k], sizeof buf[k], "%s/%s", g_root, name);
    return buf[k];
}

static void put(const char *name, const char *text)
{
    FILE *f = fopen(at(name), "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static int valid(const char *s) { return hj_valid(s, strlen(s)); }

static char *slurp(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return NULL;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

int main(void)
{
    char s[256];
    const char *why, *v;
    long n;
    int src, i;

    CHECK(mkdtemp(g_root) != NULL);

    printf("JSON: valid and invalid documents\n");
    CHECK(valid("{}") && valid("[]") && valid(" {\"a\":[1,-2.5e3,true,false,null,\"x\"]} "));
    CHECK(valid("\"\\u00e9\\ud83d\\ude00\\n\""));
    CHECK(!valid("") && !valid("{") && !valid("{\"a\":}") && !valid("{\"a\" 1}") && !valid("[1,]"));
    CHECK(!valid("{} {}") && !valid("01") && !valid("1.") && !valid("tru") && !valid("\"\\x\"") && !valid("\"a\nb\""));
    CHECK(!valid("{'a':1}") && !valid("{\"a\":1,}"));
    {
        char deep[64];
        memset(deep, '[', 17); memset(deep + 17, ']', 17); deep[34] = '\0';
        CHECK(!valid(deep));                                  /* deeper than 16 */
        memset(deep, '[', 16); memset(deep + 16, ']', 16); deep[32] = '\0';
        CHECK(valid(deep));
    }
    CHECK(!hj_valid("{\"a\":1}\0", 8));                         /* embedded NUL */

    printf("JSON: finding members, strings, numbers\n");
    {
        const char *j = "{\"x\":{\"port\":1},\"id\":\"a\\\"b\",\"args\":{\"dev\":\"045E_0B13\",\"reset\":true,\"n\":-12},\"port\":8766}";
        CHECK(valid(j));
        CHECK((v = hj_find(j, strlen(j), NULL, "port")) && hj_int(v, 0, 65535, &n) && n == 8766);   /* top level, not x.port */
        CHECK((v = hj_find(j, strlen(j), NULL, "id")) && hj_string(v, s, sizeof s) && strcmp(s, "a\"b") == 0);
        CHECK((v = hj_find(j, strlen(j), "args", "dev")) && hj_string(v, s, sizeof s) && strcmp(s, "045E_0B13") == 0);
        CHECK(hj_true(hj_find(j, strlen(j), "args", "reset")));
        CHECK((v = hj_find(j, strlen(j), "args", "n")) && hj_int(v, -100, 100, &n) && n == -12);
        CHECK(!hj_find(j, strlen(j), "args", "nope") && !hj_find(j, strlen(j), "nope", "dev") && !hj_find(j, strlen(j), "id", "x"));
        CHECK(!hj_string(hj_find(j, strlen(j), NULL, "port"), s, sizeof s));   /* a number is not a string */
        CHECK(!hj_string(hj_find(j, strlen(j), NULL, "id"), s, 3));             /* does not fit */
    }
    {
        const char *u = "{\"t\":\"\\u00e9\\ud83d\\ude00\",\"z\":\"\\u0000\",\"lone\":\"\\ud800x\"}";
        CHECK(hj_string(hj_find(u, strlen(u), NULL, "t"), s, sizeof s) && strcmp(s, "\xc3\xa9\xf0\x9f\x98\x80") == 0);
        CHECK(!hj_string(hj_find(u, strlen(u), NULL, "z"), s, sizeof s));       /* no NULs */
        CHECK(!hj_string(hj_find(u, strlen(u), NULL, "lone"), s, sizeof s));    /* no lone surrogates */
    }
    CHECK(!hj_int("1.5", 0, 9, &n) && !hj_int("1e3", 0, 9999, &n) && !hj_int("99999999999", 0, 1L << 40, &n) && !hj_int("\"1\"", 0, 9, &n));

    printf("the card: the port, and every reason not to use it\n");
    put("card.json", "{\"hub_api\":1,\"id\":\"master-control\",\"name\":\"Master Control\",\"port\":8770,\"health\":\"/api/health\"}");
    CHECK(hubcard_read_port(at("card.json"), &why) == 8770);
    CHECK(hubcard_read_port(at("missing.json"), &why) == 0 && strcmp(why, "no card") == 0);
    put("bad.json", "{\"id\":\"master-control\",\"port\":8770");
    CHECK(hubcard_read_port(at("bad.json"), &why) == 0 && strstr(why, "JSON"));
    put("arr.json", "[{\"id\":\"master-control\",\"port\":8770}]");
    CHECK(hubcard_read_port(at("arr.json"), &why) == 0 && strstr(why, "JSON object"));
    put("other.json", "{\"id\":\"omni\",\"port\":8770}");
    CHECK(hubcard_read_port(at("other.json"), &why) == 0 && strstr(why, "another app"));
    put("noport.json", "{\"id\":\"master-control\"}");
    CHECK(hubcard_read_port(at("noport.json"), &why) == 0 && strstr(why, "no port"));
    put("strport.json", "{\"id\":\"master-control\",\"port\":\"8770\"}");
    CHECK(hubcard_read_port(at("strport.json"), &why) == 0);
    put("nullport.json", "{\"id\":\"master-control\",\"port\":null}");
    CHECK(hubcard_read_port(at("nullport.json"), &why) == 0);
    for (i = 0; i < 5; i++) {
        static const int reserved[] = { 8760, 9021, 2121, 80, 1023 };
        snprintf(s, sizeof s, "{\"id\":\"master-control\",\"port\":%d}", reserved[i]);
        put("res.json", s);
        CHECK(hubcard_read_port(at("res.json"), &why) == 0 && strstr(why, "reserved"));
    }
    put("big.json", "");
    {
        FILE *f = fopen(at("big.json"), "wb");
        if (f) { fputs("{\"id\":\"master-control\",\"port\":8770,\"pad\":\"", f); for (i = 0; i < 70000; i++) fputc('x', f); fputs("\"}", f); fclose(f); }
        CHECK(hubcard_read_port(at("big.json"), &why) == 0);
    }
    CHECK(hubcard_port_ok(8766) && hubcard_port_ok(8095) && !hubcard_port_ok(8760) && !hubcard_port_ok(70000));

    printf("resolving: override, then card, then 8766 (never 8095)\n");
    CHECK(hubcard_resolve_port(at("no-override"), at("missing.json"), &src, &why) == 8766 && src == HUB_PORT_DEFAULT);
    CHECK(hubcard_resolve_port(NULL, NULL, &src, NULL) == HUB_DEFAULT_PORT && src == HUB_PORT_DEFAULT);
    CHECK(hubcard_resolve_port(at("no-override"), at("card.json"), &src, &why) == 8770 && src == HUB_PORT_CARD);
    put("override", "8771\n");
    CHECK(hubcard_resolve_port(at("override"), at("card.json"), &src, &why) == 8771 && src == HUB_PORT_OVERRIDE);
    put("override", "9021");
    CHECK(hubcard_resolve_port(at("override"), at("card.json"), &src, &why) == 8770 && src == HUB_PORT_CARD);
    put("override", "87x1");
    CHECK(hubcard_resolve_port(at("override"), at("card.json"), &src, &why) == 8770);
    put("override", "8095");                                   /* only an explicit override can bring 8095 back */
    CHECK(hubcard_resolve_port(at("override"), at("missing.json"), &src, &why) == 8095 && src == HUB_PORT_OVERRIDE);

    printf("the runtime file: written whole, folders made, removed\n");
    {
        char rt[300];
        snprintf(rt, sizeof rt, "%s/hub/runtime/master-control.json", g_root);
        CHECK(hubcard_runtime_write(rt, 8766, 4242, 1793600000L, "0.4.3"));
        CHECK((v = slurp(rt)) != NULL && valid(v));
        if (v) {
            CHECK(strstr(v, "\"id\":\"master-control\"") && strstr(v, "\"port\":8766") && strstr(v, "\"pid\":4242") &&
                  strstr(v, "\"started_at\":1793600000") && strstr(v, "\"version\":\"0.4.3\"") && strstr(v, "\"hub_api\":1"));
        }
        CHECK(hubcard_runtime_write(rt, 8770, 4243, 1793600001L, "0.4\"3\\"));   /* replaced, and quotes can't break it */
        CHECK((v = slurp(rt)) != NULL && valid(v) && strstr(v, "\"port\":8770") && strstr(v, "\"version\":\"0.43\""));
        {
            char tmp[320];
            snprintf(tmp, sizeof tmp, "%s.tmp.4243", rt);
            CHECK(access(tmp, F_OK) != 0);                                     /* no temporary left */
        }
        CHECK(hubcard_runtime_remove(rt) && access(rt, F_OK) != 0);
        CHECK(hubcard_runtime_remove(rt));                                     /* already gone: fine */
        put("blocker", "x");                                                   /* a file where a folder must go */
        CHECK(!hubcard_runtime_write(at("blocker/x/y.json"), 1, 1, 1, "v"));
    }

    printf("the token: exact match only\n");
    put("token", "0123456789abcdef0123456789abcdef\n");
    CHECK(hubcard_token_ok(at("token"), "0123456789abcdef0123456789abcdef"));
    CHECK(!hubcard_token_ok(at("token"), "0123456789abcdef0123456789abcdee"));
    CHECK(!hubcard_token_ok(at("token"), "0123456789abcdef0123456789abcde"));
    CHECK(!hubcard_token_ok(at("token"), "0123456789abcdef0123456789abcdef0"));
    CHECK(!hubcard_token_ok(at("token"), ""));
    CHECK(!hubcard_token_ok(at("token"), NULL));
    CHECK(!hubcard_token_ok(at("no-token"), "0123456789abcdef0123456789abcdef"));
    put("short-token", "0123");
    CHECK(!hubcard_token_ok(at("short-token"), "0123"));
    put("bad-token", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");
    CHECK(!hubcard_token_ok(at("bad-token"), "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));
    put("empty-token", "");
    CHECK(!hubcard_token_ok(at("empty-token"), ""));

    printf("intent names from Bloom Core's bodies\n");
    {
        const char *b = "{\"hub_api\":1,\"intent\":\"overlay.show\",\"args\":{},\"from\":\"omni\"}";
        CHECK(hubcard_intent_name(b, strlen(b), s, sizeof s) && strcmp(s, "overlay.show") == 0);
        b = "{\"hub_api\":1,\"args\":{\"intent\":\"x\"}}";
        CHECK(!hubcard_intent_name(b, strlen(b), s, sizeof s));                /* only the top level counts */
        b = "{\"intent\":7}";
        CHECK(!hubcard_intent_name(b, strlen(b), s, sizeof s));
        b = "{\"intent\":\"\"}";
        CHECK(!hubcard_intent_name(b, strlen(b), s, sizeof s));
        b = "{\"intent\":\"overlay.show\"";
        CHECK(!hubcard_intent_name(b, strlen(b), s, sizeof s));
        b = "\"overlay.show\"";
        CHECK(!hubcard_intent_name(b, strlen(b), s, sizeof s));
    }

    printf("autoload mentions (for the migration note)\n");
    put("autoload.txt", "kstuff-lite.elf\n!1000\nMaster-Control.elf\n");
    CHECK(hubcard_autoload_mentions(at("autoload.txt")) == 1);
    put("autoload2.txt", "padbridge-0.3.16.elf\n");
    CHECK(hubcard_autoload_mentions(at("autoload2.txt")) == 0);
    CHECK(hubcard_autoload_mentions(at("no-autoload.txt")) == -1);

    printf("mkdirs\n");
    CHECK(hubcard_mkdirs(at("a/b/c/d")) && hubcard_mkdirs(at("a/b/c/d")));
    put("file", "x");
    CHECK(!hubcard_mkdirs(at("file/sub")));

    printf("random bytes through the JSON reader (no crash, no false accept of junk)\n");
    {
        static const char alphabet[] = "{}[]\":,\\ 0123456789.-+eEtrufalsn\"\\u00e9xyz\n\t";
        char buf[96];
        unsigned seed = 12345;
        long accepted = 0;
        for (i = 0; i < 200000; i++) {
            int len = (int)((seed = seed * 1103515245u + 12345u) >> 16) % (int)sizeof buf, k;
            for (k = 0; k < len; k++) buf[k] = alphabet[((seed = seed * 1103515245u + 12345u) >> 16) % (sizeof alphabet - 1)];
            if (hj_valid(buf, (size_t)len)) {
                accepted++;
                (void)hj_find(buf, (size_t)len, NULL, "intent");
                (void)hj_find(buf, (size_t)len, "args", "dev");
            }
            (void)hubcard_intent_name(buf, (size_t)len, s, sizeof s);
        }
        printf("  %ld of 200000 accepted as JSON\n", accepted);
    }

    printf("the log keeps three rotations\n");
    {
        char p1[320];
        CHECK(log_path() == NULL);
        CHECK(log_open(at("mc.log")));
        CHECK(log_path() && strcmp(log_path(), at("mc.log")) == 0);
        for (i = 0; i < 4 * 1024 * 1024 / 100 + 2000; i++)
            log_line("%096d", i);                     /* ~100 bytes a line: four times past 1 MB */
        log_close();
        for (i = 1; i <= 4; i++) {
            snprintf(p1, sizeof p1, "%s.%d", at("mc.log"), i);
            if (i <= 3) CHECK(access(p1, F_OK) == 0);
            else CHECK(access(p1, F_OK) != 0);
        }
        CHECK(access(at("mc.log"), F_OK) == 0);
    }

    if (!g_fail) printf("all hubcard checks passed\n");
    else printf("hubcard: %d FAILED\n", g_fail);
    return g_fail != 0;
}
