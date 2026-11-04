/* Button remapping per controller model. See remap.h. */
#include "remap.h"
#include "log.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define RING 4                          /* compiled maps per model */

/* Sources, in index order; the first REMAP_BTN_N are also the targets. */
static const char *const k_src_name[REMAP_SRC_N] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3",
    "options", "create", "touchpad", "ps", "up", "down", "left", "right",
    "p1", "p2", "p3", "p4", "m5", "m6",
};
static const uint32_t k_btn_bit[REMAP_BTN_N] = {
    PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE, PAD_L1, PAD_R1, PAD_L2, PAD_R2,
    PAD_L3, PAD_R3, PAD_OPTIONS, PAD_CREATE, PAD_TOUCHPAD, PAD_PS,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
};
enum { SRC_L2 = 6, SRC_R2 = 7 };
#define BTN_MASK (PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE | PAD_L1 | PAD_R1 | \
                  PAD_L2 | PAD_R2 | PAD_L3 | PAD_R3 | PAD_OPTIONS | PAD_CREATE | \
                  PAD_TOUCHPAD | PAD_PS | PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)

static const struct { const char *alias; int src; } k_alias[] = {
    { "share", 11 }, { "m1", 18 }, { "m2", 19 }, { "m3", 20 }, { "m4", 21 },
};

/* A map ready for remap_apply. */
typedef struct {
    uint32_t tbit[REMAP_SRC_N];         /* target bit per source, 0: none */
    uint32_t btn_to_l2, btn_to_r2;      /* pressed non-trigger buttons that give a full L2 / R2 */
    uint8_t  x_to_l2, x_to_r2;          /* the same for extra buttons */
    uint8_t  noop;                      /* identity, no options: nothing to do */
    uint8_t  swap_sticks, invert_ly, invert_ry, swap_triggers, hair_l2, hair_r2;
} compiled;

typedef struct {
    uint32_t key;                       /* vid << 16 | pid; set once, before g_count covers it */
    int cur;                            /* ring index in use, -1: no map (atomic) */
    int next;                           /* the ring slot the next save fills */
    int seen, saved;
    char name[REMAP_NAME_MAX];
    remap_map edit;                     /* the map as given, for the menu */
    compiled ring[RING];
} entry;

static entry g_e[REMAP_MODELS];
static int g_count;                     /* entries in use (atomic) */
static int g_active;                    /* entries with a map (atomic) */
static char g_lock;                     /* writers only */
static char g_dir[256];

static void lock(void)
{
    while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE)) sched_yield();
}

static void unlock(void)
{
    __atomic_clear(&g_lock, __ATOMIC_RELEASE);
}

/* ---- maps ------------------------------------------------------------------ */

void remap_default(remap_map *m)
{
    int i;

    memset(m, 0, sizeof *m);
    for (i = 0; i < REMAP_SRC_N; i++) m->target[i] = i < REMAP_BTN_N ? (uint8_t)i : REMAP_NONE;
}

const char *remap_source_name(int i)
{
    return i >= 0 && i < REMAP_SRC_N ? k_src_name[i] : NULL;
}

static void compile(const remap_map *m, compiled *c)
{
    remap_map def;
    int i;

    memset(c, 0, sizeof *c);
    remap_default(&def);
    for (i = 0; i < REMAP_SRC_N; i++) {
        uint8_t t = m->target[i];
        uint32_t bit = t < REMAP_BTN_N ? k_btn_bit[t] : 0;
        c->tbit[i] = bit;
        if (i == SRC_L2 || i == SRC_R2) continue;     /* their analog value goes instead */
        if (i < REMAP_BTN_N) {
            if (bit == PAD_L2) c->btn_to_l2 |= k_btn_bit[i];
            if (bit == PAD_R2) c->btn_to_r2 |= k_btn_bit[i];
        } else {
            if (bit == PAD_L2) c->x_to_l2 |= (uint8_t)(1u << (i - REMAP_BTN_N));
            if (bit == PAD_R2) c->x_to_r2 |= (uint8_t)(1u << (i - REMAP_BTN_N));
        }
    }
    c->swap_sticks = !!m->swap_sticks;
    c->invert_ly = !!m->invert_ly;
    c->invert_ry = !!m->invert_ry;
    c->swap_triggers = !!m->swap_triggers;
    c->hair_l2 = !!m->hair_l2;
    c->hair_r2 = !!m->hair_r2;
    c->noop = memcmp(m->target, def.target, sizeof def.target) == 0 &&
              !c->swap_sticks && !c->invert_ly && !c->invert_ry && !c->swap_triggers &&
              !c->hair_l2 && !c->hair_r2;
}

static uint8_t invert_axis(uint8_t v)
{
    return v == 0 ? 255 : (uint8_t)(256 - v);         /* 128 stays 128 */
}

static void run(const compiled *c, pad_state *st)
{
    uint32_t in = st->buttons, out, pressed;
    uint8_t l2 = st->l2, r2 = st->r2, ol2 = 0, or2 = 0, x = st->extra;
    int i;

    if (c->noop) return;
    if (c->swap_triggers) {
        uint8_t t = l2;
        l2 = r2;
        r2 = t;
        in = (in & ~(uint32_t)(PAD_L2 | PAD_R2)) | (in & PAD_L2 ? PAD_R2 : 0) | (in & PAD_R2 ? PAD_L2 : 0);
    }

    out = in & ~(uint32_t)BTN_MASK;
    pressed = in & BTN_MASK;
    for (i = 0; pressed && i < REMAP_BTN_N; i++)
        if (pressed & k_btn_bit[i]) {
            out |= c->tbit[i];
            pressed &= ~k_btn_bit[i];
        }
    for (i = 0; x && i < REMAP_SRC_N - REMAP_BTN_N; i++)
        if (x & (1u << i)) out |= c->tbit[REMAP_BTN_N + i];

    /* analog triggers follow where the trigger buttons went */
    if (c->tbit[SRC_L2] == PAD_L2) ol2 = l2;
    else if (c->tbit[SRC_L2] == PAD_R2) or2 = l2;
    if (c->tbit[SRC_R2] == PAD_R2 && r2 > or2) or2 = r2;
    else if (c->tbit[SRC_R2] == PAD_L2 && r2 > ol2) ol2 = r2;
    if ((in & c->btn_to_l2) || (x & c->x_to_l2)) ol2 = 255;
    if ((in & c->btn_to_r2) || (x & c->x_to_r2)) or2 = 255;

    if (c->hair_l2) {
        if (ol2 > REMAP_HAIR || (out & PAD_L2)) { ol2 = 255; out |= PAD_L2; }
        else ol2 = 0;
    }
    if (c->hair_r2) {
        if (or2 > REMAP_HAIR || (out & PAD_R2)) { or2 = 255; out |= PAD_R2; }
        else or2 = 0;
    }
    st->buttons = out;
    st->l2 = ol2;
    st->r2 = or2;

    if (c->swap_sticks) {
        uint8_t a = st->lx, b = st->ly;
        st->lx = st->rx;
        st->ly = st->ry;
        st->rx = a;
        st->ry = b;
    }
    if (c->invert_ly) st->ly = invert_axis(st->ly);
    if (c->invert_ry) st->ry = invert_axis(st->ry);
}

void remap_apply(uint16_t vid, uint16_t pid, pad_state *st)
{
    uint32_t key = (uint32_t)vid << 16 | pid;
    int n, i;

    if (__atomic_load_n(&g_active, __ATOMIC_ACQUIRE) == 0) return;
    n = __atomic_load_n(&g_count, __ATOMIC_ACQUIRE);
    for (i = 0; i < n; i++)
        if (g_e[i].key == key) {
            int c = __atomic_load_n(&g_e[i].cur, __ATOMIC_ACQUIRE);
            if (c >= 0 && c < RING) run(&g_e[i].ring[c], st);
            return;
        }
}

/* ---- text ---------------------------------------------------------------- */

static char *trim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
    return s;
}

static int source_index(const char *s)
{
    size_t i;

    for (i = 0; i < REMAP_SRC_N; i++) if (strcasecmp(s, k_src_name[i]) == 0) return (int)i;
    for (i = 0; i < sizeof k_alias / sizeof k_alias[0]; i++)
        if (strcasecmp(s, k_alias[i].alias) == 0) return k_alias[i].src;
    return -1;
}

static int target_index(const char *s)
{
    int i = source_index(s);

    if (strcasecmp(s, "none") == 0) return REMAP_NONE;
    return i >= 0 && i < REMAP_BTN_N ? i : -1;
}

static int flag_value(const char *s)
{
    static const char *const on[] = { "1", "on", "yes", "true" }, *const off[] = { "0", "off", "no", "false" };
    size_t i;

    for (i = 0; i < 4; i++) {
        if (strcasecmp(s, on[i]) == 0) return 1;
        if (strcasecmp(s, off[i]) == 0) return 0;
    }
    return -1;
}

static void copy_name(char *out, size_t max, const char *s)
{
    size_t n = 0;

    if (!out || !max) return;
    for (; s && *s && n + 1 < max; s++)
        if ((unsigned char)*s >= 0x20 && *s != 0x7F && *s != '"' && *s != '\\') out[n++] = *s;
    out[n] = '\0';
}

static int parse_line(char *line, remap_map *m, char *name, size_t name_max)
{
    static const struct { const char *key; size_t off; } opts[] = {
        { "swap_sticks", offsetof(remap_map, swap_sticks) },
        { "invert_ly", offsetof(remap_map, invert_ly) },
        { "invert_ry", offsetof(remap_map, invert_ry) },
        { "swap_triggers", offsetof(remap_map, swap_triggers) },
        { "hair_l2", offsetof(remap_map, hair_l2) },
        { "hair_r2", offsetof(remap_map, hair_r2) },
    };
    char *eq, *key, *val;
    size_t i;
    int s, t;

    line = trim(line);
    if (!*line || *line == '#') return 1;
    eq = strchr(line, '=');
    if (!eq) return 0;
    *eq = '\0';
    key = trim(line);
    val = trim(eq + 1);
    if (strcasecmp(key, "name") == 0) {
        copy_name(name, name_max, val);
        return 1;
    }
    for (i = 0; i < sizeof opts / sizeof opts[0]; i++)
        if (strcasecmp(key, opts[i].key) == 0) {
            int f = flag_value(val);
            if (f < 0) return 0;
            ((uint8_t *)m)[opts[i].off] = (uint8_t)f;
            return 1;
        }
    s = source_index(key);
    t = target_index(val);
    if (s < 0 || t < 0) return 0;
    m->target[s] = (uint8_t)t;
    return 1;
}

int remap_parse(const char *text, remap_map *m, char *name, size_t name_max, int *bad_line)
{
    int no = 0;

    if (bad_line) *bad_line = 0;
    if (!text) return 1;
    if (strlen(text) > REMAP_TEXT_MAX) return 0;
    while (*text) {
        char line[160];
        const char *nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        no++;
        if (len >= sizeof line) {
            if (bad_line) *bad_line = no;
            return 0;
        }
        memcpy(line, text, len);
        line[len] = '\0';
        if (!parse_line(line, m, name, name_max)) {
            if (bad_line) *bad_line = no;
            return 0;
        }
        text += len + (nl ? 1 : 0);
    }
    return 1;
}

int remap_format(const remap_map *m, const char *name, char *out, size_t max)
{
    static const char *const opt[] = { "swap_sticks", "invert_ly", "invert_ry", "swap_triggers", "hair_l2", "hair_r2" };
    const uint8_t optv[] = { m->swap_sticks, m->invert_ly, m->invert_ry, m->swap_triggers, m->hair_l2, m->hair_r2 };
    remap_map def;
    size_t n = 0;
    int i, k;

#define OUT(...) do { \
        k = snprintf(out + n, max - n, __VA_ARGS__); \
        if (k < 0 || (size_t)k >= max - n) return -1; \
        n += (size_t)k; \
    } while (0)
    if (!out || !max) return -1;
    out[0] = '\0';
    remap_default(&def);
    if (name && *name) {
        char clean[REMAP_NAME_MAX];
        copy_name(clean, sizeof clean, name);
        OUT("name = %s\n", clean);
    }
    for (i = 0; i < REMAP_SRC_N; i++) {
        uint8_t t = m->target[i] < REMAP_BTN_N ? m->target[i] : REMAP_NONE;
        if (t == def.target[i]) continue;
        OUT("%s = %s\n", k_src_name[i], t == REMAP_NONE ? "none" : k_src_name[t]);
    }
    for (i = 0; i < 6; i++) if (optv[i]) OUT("%s = 1\n", opt[i]);
#undef OUT
    return (int)n;
}

/* ---- the table ----------------------------------------------------------- */

/* With the lock held. NULL if new and the table is full. */
static entry *find(uint32_t key, int create)
{
    int n = g_count, i;

    for (i = 0; i < n; i++) if (g_e[i].key == key) return &g_e[i];
    if (!create || n >= REMAP_MODELS) return NULL;
    memset(&g_e[n], 0, sizeof g_e[n]);
    g_e[n].key = key;
    g_e[n].cur = -1;
    remap_default(&g_e[n].edit);
    __atomic_store_n(&g_count, n + 1, __ATOMIC_RELEASE);
    return &g_e[n];
}

/* With the lock held: makes `m` the live map of `e`. */
static void publish(entry *e, const remap_map *m)
{
    int slot = e->next;

    compile(m, &e->ring[slot]);
    e->next = (slot + 1) % RING;
    e->edit = *m;
    if (e->cur < 0) __atomic_add_fetch(&g_active, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&e->cur, slot, __ATOMIC_RELEASE);
    e->saved = 1;
}

static void unpublish(entry *e)
{
    if (e->cur >= 0) {
        __atomic_store_n(&e->cur, -1, __ATOMIC_RELEASE);
        __atomic_sub_fetch(&g_active, 1, __ATOMIC_RELEASE);
    }
    e->saved = 0;
    remap_default(&e->edit);
}

static void path_of(char *out, size_t max, uint16_t vid, uint16_t pid, const char *suffix)
{
    snprintf(out, max, "%s/%04X_%04X.map%s", g_dir, vid, pid, suffix);
}

/* Reads a whole small file. Returns 1 if it fits. */
static int read_text(const char *path, char *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    int too_long;

    if (!f) return 0;
    n = fread(buf, 1, max - 1, f);
    too_long = n == max - 1 && fgetc(f) != EOF;
    fclose(f);
    buf[n] = '\0';
    return !too_long && strlen(buf) == n;       /* and no NUL inside */
}

static int write_text(uint16_t vid, uint16_t pid, const char *text)
{
    char path[300], tmp[300];
    size_t len = strlen(text);
    FILE *f;
    int ok;

    path_of(path, sizeof path, vid, pid, "");
    path_of(tmp, sizeof tmp, vid, pid, ".tmp");
    f = fopen(tmp, "wb");
    if (!f) return 0;
    ok = fwrite(text, 1, len, f) == len;
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (ok && rename(tmp, path) == 0) return 1;
    unlink(tmp);
    return 0;
}

/* "VVVV_PPPP.map", either case. */
static int file_ids(const char *s, uint16_t *vid, uint16_t *pid)
{
    unsigned v = 0, p = 0;
    int i;

    if (strlen(s) != 13 || s[4] != '_' || strcasecmp(s + 9, ".map") != 0) return 0;
    for (i = 0; i < 9; i++) {
        int c = (unsigned char)s[i], d;
        if (i == 4) continue;
        if (!isxdigit(c)) return 0;
        d = isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10);
        if (i < 4) v = v << 4 | (unsigned)d;
        else p = p << 4 | (unsigned)d;
    }
    *vid = (uint16_t)v;
    *pid = (uint16_t)p;
    return 1;
}

void remap_init(const char *dir)
{
    DIR *d;
    struct dirent *de;

    memset(g_e, 0, sizeof g_e);
    g_count = 0;
    g_active = 0;
    g_dir[0] = '\0';
    if (!dir || !*dir) return;
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    {
        size_t n = strlen(g_dir);
        while (n > 1 && g_dir[n - 1] == '/') g_dir[--n] = '\0';
    }
    mkdir(g_dir, 0755);
    d = opendir(g_dir);
    if (!d) {
        log_line("remap: %s not available, maps will not be saved", g_dir);
        return;
    }
    while ((de = readdir(d)) != NULL) {
        static char text[REMAP_TEXT_MAX + 1];
        char path[sizeof g_dir + sizeof de->d_name + 2], name[REMAP_NAME_MAX] = "";
        uint16_t vid, pid;
        remap_map m;
        entry *e;
        int bad = 0;

        if (!file_ids(de->d_name, &vid, &pid)) continue;
        snprintf(path, sizeof path, "%s/%s", g_dir, de->d_name);
        remap_default(&m);
        if (!read_text(path, text, sizeof text) || !remap_parse(text, &m, name, sizeof name, &bad)) {
            log_line("remap: %s not used (unreadable or line %d not understood)", de->d_name, bad);
            continue;
        }
        e = find((uint32_t)vid << 16 | pid, 1);
        if (!e) {
            log_line("remap: more than %d models, %s not used", REMAP_MODELS, de->d_name);
            continue;
        }
        if (e->saved) continue;         /* the same ids in the other case */
        if (name[0]) snprintf(e->name, sizeof e->name, "%s", name);
        publish(e, &m);
        log_line("remap: map for %04x:%04x loaded", vid, pid);
    }
    closedir(d);
}

void remap_note_device(uint16_t vid, uint16_t pid, const char *name)
{
    entry *e;

    lock();
    e = find((uint32_t)vid << 16 | pid, 1);
    if (e) {
        e->seen = 1;
        if (name && *name) copy_name(e->name, sizeof e->name, name);
    }
    unlock();
}

int remap_set(uint16_t vid, uint16_t pid, const char *text, const char **err, int *bad_line)
{
    static char file[REMAP_TEXT_MAX + 256];
    char name[REMAP_NAME_MAX] = "";
    remap_map m;
    entry *e;
    int ok = 0;

    if (bad_line) *bad_line = 0;
    remap_default(&m);
    if (!remap_parse(text, &m, name, sizeof name, bad_line)) {
        if (err) *err = "syntax";
        return 0;
    }
    lock();
    e = find((uint32_t)vid << 16 | pid, 1);
    if (!e) {
        if (err) *err = "full";
    } else {
        if (!name[0]) snprintf(name, sizeof name, "%s", e->name);
        if (g_dir[0] && (remap_format(&m, name, file, sizeof file) < 0 || !write_text(vid, pid, file))) {
            if (err) *err = "write";
        } else {
            if (name[0]) snprintf(e->name, sizeof e->name, "%s", name);
            publish(e, &m);
            ok = 1;
        }
    }
    unlock();
    if (ok) log_line("remap: map for %04x:%04x saved", vid, pid);
    else log_line("remap: map for %04x:%04x not saved (%s)", vid, pid, err && *err ? *err : "?");
    return ok;
}

int remap_reset(uint16_t vid, uint16_t pid)
{
    char path[300];
    entry *e;
    int had = 0;

    lock();
    e = find((uint32_t)vid << 16 | pid, 0);
    if (e) {
        had = e->saved;
        unpublish(e);
    }
    if (g_dir[0]) {
        path_of(path, sizeof path, vid, pid, "");
        if (unlink(path) == 0) had = 1;
        snprintf(path, sizeof path, "%s/%04x_%04x.map", g_dir, vid, pid);
        if (unlink(path) == 0) had = 1;
    }
    unlock();
    if (had) log_line("remap: map for %04x:%04x removed", vid, pid);
    return had;
}

int remap_get(int i, remap_info *out)
{
    int k, n = 0, found = 0;

    lock();
    for (k = 0; k < g_count; k++) {
        entry *e = &g_e[k];
        if (!e->seen && !e->saved) continue;
        if (n++ != i) continue;
        out->vid = (uint16_t)(e->key >> 16);
        out->pid = (uint16_t)e->key;
        snprintf(out->name, sizeof out->name, "%s", e->name);
        out->seen = e->seen;
        out->saved = e->saved;
        out->map = e->edit;
        found = 1;
        break;
    }
    unlock();
    return found;
}
