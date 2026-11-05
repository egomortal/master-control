/* SPDX-License-Identifier: GPL-3.0-or-later
 * Master Control's side of the Omnibloom plug-in standard. See hubcard.h. */
#include "hubcard.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HJ_DEPTH      16
#define CARD_MAX      (64 * 1024)
#define AUTOLOAD_MAX  (64 * 1024)

/* ---- JSON ------------------------------------------------------------------ */

static const char *ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* p at '"'. Returns the character after the closing quote, or NULL. */
static const char *skip_string(const char *p, const char *e)
{
    if (p >= e || *p != '"') return NULL;
    for (p++; p < e; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') return p + 1;
        if (c < 0x20) return NULL;
        if (c == '\\') {
            if (++p >= e) return NULL;
            switch (*p) {
            case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't': break;
            case 'u': {
                int i;
                for (i = 1; i <= 4; i++)
                    if (p + i >= e || hexval(p[i]) < 0) return NULL;
                p += 4;
                break;
            }
            default: return NULL;
            }
        }
    }
    return NULL;
}

static const char *skip_number(const char *p, const char *e)
{
    const char *s;

    if (p < e && *p == '-') p++;
    if (p >= e) return NULL;
    if (*p == '0') p++;
    else if (*p >= '1' && *p <= '9') { while (p < e && isdigit((unsigned char)*p)) p++; }
    else return NULL;
    if (p < e && *p == '.') {
        s = ++p;
        while (p < e && isdigit((unsigned char)*p)) p++;
        if (p == s) return NULL;
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < e && (*p == '+' || *p == '-')) p++;
        s = p;
        while (p < e && isdigit((unsigned char)*p)) p++;
        if (p == s) return NULL;
    }
    return p;
}

static const char *skip_value(const char *p, const char *e, int depth)
{
    p = ws(p, e);
    if (p >= e || depth > HJ_DEPTH) return NULL;
    switch (*p) {
    case '"': return skip_string(p, e);
    case '{':
        if (depth >= HJ_DEPTH) return NULL;     /* at most HJ_DEPTH nested containers */
        p = ws(p + 1, e);
        if (p < e && *p == '}') return p + 1;
        for (;;) {
            p = skip_string(ws(p, e), e);
            if (!p) return NULL;
            p = ws(p, e);
            if (p >= e || *p != ':') return NULL;
            p = skip_value(p + 1, e, depth + 1);
            if (!p) return NULL;
            p = ws(p, e);
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == '}') return p + 1;
            return NULL;
        }
    case '[':
        if (depth >= HJ_DEPTH) return NULL;
        p = ws(p + 1, e);
        if (p < e && *p == ']') return p + 1;
        for (;;) {
            p = skip_value(p, e, depth + 1);
            if (!p) return NULL;
            p = ws(p, e);
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == ']') return p + 1;
            return NULL;
        }
    case 't': return (size_t)(e - p) >= 4 && memcmp(p, "true", 4) == 0 ? p + 4 : NULL;
    case 'f': return (size_t)(e - p) >= 5 && memcmp(p, "false", 5) == 0 ? p + 5 : NULL;
    case 'n': return (size_t)(e - p) >= 4 && memcmp(p, "null", 4) == 0 ? p + 4 : NULL;
    default:  return skip_number(p, e);
    }
}

int hj_valid(const char *text, size_t len)
{
    const char *e = text + len, *p;

    if (!text || memchr(text, '\0', len)) return 0;
    p = skip_value(text, e, 0);
    return p && ws(p, e) == e;
}

/* Compares the JSON string at p (validated) with the plain C string key. */
static int string_is(const char *p, const char *key)
{
    char buf[128];

    return hj_string(p, buf, sizeof buf) && strcmp(buf, key) == 0;
}

/* p at '{' (validated text). The value of `key`, or NULL. */
static const char *find_member(const char *p, const char *e, const char *key)
{
    p = ws(p, e);
    if (p >= e || *p != '{') return NULL;
    p = ws(p + 1, e);
    if (p < e && *p == '}') return NULL;
    for (;;) {
        const char *k = ws(p, e), *v;
        p = skip_string(k, e);
        if (!p) return NULL;
        p = ws(p, e);
        if (p >= e || *p != ':') return NULL;
        v = ws(p + 1, e);
        if (string_is(k, key)) return v;
        p = skip_value(v, e, 1);
        if (!p) return NULL;
        p = ws(p, e);
        if (p < e && *p == ',') { p++; continue; }
        return NULL;
    }
}

int hj_count(const char *json, size_t len, const char *obj, const char *key)
{
    const char *e=json+len,*p=json;int count=0;
    if(obj){p=find_member(p,e,obj);if(!p)return 0;}
    p=ws(p,e);if(p>=e||*p!='{')return 0;p=ws(p+1,e);
    while(p<e&&*p!='}') {
        const char *k=p; p=skip_string(p,e);if(!p)return 0;
        p=ws(p,e);if(p>=e||*p++!=':')return 0;
        if(string_is(k,key))count++;
        p=skip_value(ws(p,e),e,1);if(!p)return 0;
        p=ws(p,e);if(p<e&&*p==',')p=ws(p+1,e);else break;
    }
    return count;
}

const char *hj_find(const char *json, size_t len, const char *obj, const char *key)
{
    const char *e = json + len, *p = json;

    if (obj) {
        p = find_member(p, e, obj);
        if (!p || *p != '{') return NULL;
    }
    return find_member(p, e, key);
}

static size_t put_utf8(char *out, unsigned cp)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static unsigned hex4(const char *p)
{
    return (unsigned)(hexval(p[0]) << 12 | hexval(p[1]) << 8 | hexval(p[2]) << 4 | hexval(p[3]));
}

int hj_string(const char *p, char *out, size_t max)
{
    size_t n = 0;

    if (!p || *p != '"' || max == 0) return 0;
    for (p++; *p != '"'; p++) {
        char tmp[4];
        size_t k = 1;
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'b': tmp[0] = '\b'; break;
            case 'f': tmp[0] = '\f'; break;
            case 'n': tmp[0] = '\n'; break;
            case 'r': tmp[0] = '\r'; break;
            case 't': tmp[0] = '\t'; break;
            case 'u': {
                unsigned cp = hex4(p + 1);
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && p[1] == '\\' && p[2] == 'u') {
                    unsigned lo = hex4(p + 3);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;   /* no NULs, no lone surrogates */
                k = put_utf8(tmp, cp);
                break;
            }
            default: tmp[0] = *p; break;                                 /* " \ / */
            }
        } else {
            tmp[0] = *p;
        }
        if (n + k >= max) return 0;
        memcpy(out + n, tmp, k);
        n += k;
    }
    out[n] = '\0';
    return 1;
}

int hj_int(const char *p, long lo, long hi, long *out)
{
    long v = 0;
    int neg = 0, digits = 0;

    if (!p) return 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') {
        if (v > 100000000L) return 0;
        v = v * 10 + (*p++ - '0');
        digits++;
    }
    if (!digits || *p == '.' || *p == 'e' || *p == 'E') return 0;
    if (neg) v = -v;
    if (v < lo || v > hi) return 0;
    *out = v;
    return 1;
}

int hj_true(const char *p)
{
    return p && strncmp(p, "true", 4) == 0;
}

/* ---- files ----------------------------------------------------------------- */

/* Reads at most max bytes; returns a NUL-terminated buffer (free it) or NULL
 * if missing, unreadable or larger than max. */
static char *slurp(const char *path, size_t max, size_t *len)
{
    struct stat st;
    FILE *f;
    char *buf;
    size_t n;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (size_t)st.st_size > max) return NULL;
    if (!(f = fopen(path, "rb"))) return NULL;
    buf = malloc((size_t)st.st_size + 1);
    if (!buf) { fclose(f); return NULL; }
    n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

int hubcard_port_ok(long port)
{
    return port >= 1024 && port <= 65535 && port != 8760 && port != 9021 && port != 2121;
}

int hubcard_read_port(const char *path, const char **why)
{
    size_t len = 0;
    char *card = slurp(path, CARD_MAX, &len), id[64];
    const char *v;
    long port;
    int out = 0;
    const char *dummy;

    if (!why) why = &dummy;
    if (!card) {
        *why = access(path, F_OK) == 0 ? "the card could not be read (or is over 64 KB)" : "no card";
        return 0;
    }
    if (!hj_valid(card, len) || *ws(card, card + len) != '{') *why = "the card is not a JSON object";
    else if (!(v = hj_find(card, len, NULL, "id")) || !hj_string(v, id, sizeof id) || strcmp(id, HUB_APP_ID) != 0)
        *why = "the card is for another app";
    else if (!(v = hj_find(card, len, NULL, "port")) || !hj_int(v, 0, 65535, &port))
        *why = "the card has no port";
    else if (!hubcard_port_ok(port))
        *why = "the card's port is reserved or out of range";
    else
        out = (int)port;
    free(card);
    return out;
}

static int read_override(const char *path)
{
    size_t len = 0;
    char *s = slurp(path, 64, &len), *e;
    long v;

    if (!s) return 0;
    v = strtol(s, &e, 10);
    while (*e == ' ' || *e == '\n' || *e == '\r' || *e == '\t') e++;
    if (e == s || *e != '\0' || !hubcard_port_ok(v)) v = 0;
    free(s);
    return (int)v;
}

int hubcard_resolve_port(const char *override_path, const char *card_path,
                         int *source, const char **why)
{
    const char *dummy;
    int p;

    if (!why) why = &dummy;
    *why = "";
    if (override_path && (p = read_override(override_path)) > 0) {
        *source = HUB_PORT_OVERRIDE;
        return p;
    }
    if (card_path && (p = hubcard_read_port(card_path, why)) > 0) {
        *source = HUB_PORT_CARD;
        return p;
    }
    *source = HUB_PORT_DEFAULT;
    return HUB_DEFAULT_PORT;
}

int hubcard_mkdirs(const char *dir)
{
    char buf[256];
    size_t i, n = strlen(dir);
    struct stat st;

    if (n == 0 || n >= sizeof buf) return 0;
    memcpy(buf, dir, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return 0;
            buf[i] = c;
        }
    }
    return stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
}

static void json_plain(char *out, size_t max, const char *s)
{
    size_t n = 0;

    for (; s && *s && n + 1 < max; s++)
        if ((unsigned char)*s >= 0x20 && *s != '"' && *s != '\\') out[n++] = *s;
    out[n] = '\0';
}

int hubcard_runtime_write(const char *path, int port, long pid, long started_at, const char *version)
{
    char dir[256], tmp[300], ver[32];
    const char *slash = strrchr(path, '/');
    FILE *f;
    int ok;

    if (slash && slash != path) {
        size_t n = (size_t)(slash - path);
        if (n >= sizeof dir) return 0;
        memcpy(dir, path, n);
        dir[n] = '\0';
        if (!hubcard_mkdirs(dir)) return 0;
    }
    json_plain(ver, sizeof ver, version);
    snprintf(tmp, sizeof tmp, "%s.tmp.%ld", path, pid);
    if (!(f = fopen(tmp, "w"))) return 0;
    ok = fprintf(f, "{\"hub_api\":1,\"id\":\"" HUB_APP_ID "\",\"port\":%d,\"pid\":%ld,"
                    "\"started_at\":%ld,\"version\":\"%s\"}\n", port, pid, started_at, ver) > 0;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return 0;
    }
    return 1;
}

int hubcard_runtime_remove(const char *path)
{
    return unlink(path) == 0 || errno == ENOENT;
}

static int is_token(const char *s, size_t n)
{
    size_t i;

    if (n != 32) return 0;
    for (i = 0; i < n; i++)
        if (hexval(s[i]) < 0) return 0;
    return 1;
}

int hubcard_token_ok(const char *token_path, const char *presented)
{
    size_t len = 0, i;
    char *t;
    unsigned diff = 0;
    int ok;

    if (!presented || !is_token(presented, strlen(presented))) return 0;
    if (!(t = slurp(token_path, 128, &len))) return 0;
    while (len > 0 && (t[len - 1] == '\n' || t[len - 1] == '\r' || t[len - 1] == ' ' || t[len - 1] == '\t')) t[--len] = '\0';
    ok = is_token(t, len);
    for (i = 0; ok && i < 32; i++) diff |= (unsigned)(t[i] ^ presented[i]);
    free(t);
    return ok && diff == 0;
}

int hubcard_intent_name(const char *body, size_t len, char *out, size_t max)
{
    const char *v;

    if (!hj_valid(body, len) || *ws(body, body + len) != '{') return 0;
    v = hj_find(body, len, NULL, "intent");
    return v && hj_string(v, out, max) && out[0];
}

int hubcard_autoload_mentions(const char *autoload_path)
{
    size_t len = 0, i;
    char *s = slurp(autoload_path, AUTOLOAD_MAX, &len);
    int found;

    if (!s) return -1;
    for (i = 0; i < len; i++) s[i] = (char)tolower((unsigned char)s[i]);
    found = strstr(s, "master-control") || strstr(s, "master_control") || strstr(s, "mastercontrol");
    free(s);
    return found ? 1 : 0;
}
