#include "xinput.h"

#include <string.h>

#define TRIGGER_PRESS 0x20          /* as in profiles.c */

#define GIP_CMD_ACK          0x01
#define GIP_CMD_ANNOUNCE     0x02
#define GIP_CMD_POWER        0x05
#define GIP_CMD_AUTHENTICATE 0x06
#define GIP_CMD_VIRTUAL_KEY  0x07
#define GIP_CMD_RUMBLE       0x09
#define GIP_CMD_LED          0x0a
#define GIP_CMD_FIRMWARE     0x0c
#define GIP_CMD_INPUT        0x20

#define GIP_OPT_ACK          0x10
#define GIP_OPT_INTERNAL     0x20

static int16_t s16le(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }
static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Signed 16-bit axis -> 0..255 with 128 at rest. */
static uint8_t axis_x(int16_t v) { return (uint8_t)(((int32_t)v + 32768) >> 8); }

/* Same, flipped: Xbox Y grows upwards, the DualSense's downwards. */
static uint8_t axis_y(int16_t v)
{
    int32_t r = (32768 - (int32_t)v) >> 8;
    return (uint8_t)(r > 255 ? 255 : r);
}

int xi_kind_of_interface(uint8_t cls, uint8_t sub, uint8_t proto)
{
    if (cls != 0xFF) return XI_NONE;
    if (sub == 0x47 && proto == 0xD0) return XI_GIP;
    if (sub == 0x5D && (proto == 0x01 || proto == 0x81)) return XI_X360;
    return XI_NONE;
}

void xi_setup(xi_pad *p, int kind, uint16_t vid, uint16_t pid, uint16_t bcd)
{
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->vid = vid;
    p->pid = pid;
    p->bcd = bcd;
    p->out_seq = 1;
    if (kind != XI_GIP) return;
    if (vid == 0x045E && pid == 0x0B12) p->share_offset = 26;      /* Series X|S */
    else {
        /* Licensed pads with a Share button (xpad's MAP_SHARE_BUTTON list). */
        static const uint16_t share18[][2] = {
            { 0x03F0, 0x08B6 }, { 0x0B05, 0x1A38 }, { 0x0B05, 0x1C96 }, { 0x0B05, 0x1D04 },
            { 0x0F0D, 0x01B2 }, { 0x10F5, 0x7008 }, { 0x10F5, 0x7073 }, { 0x20D6, 0x2064 },
            { 0x20D6, 0x400B }, { 0x20D6, 0x890B }, { 0x2DC8, 0x200F }, { 0x2DC8, 0x201E },
            { 0x2E24, 0x0423 }, { 0x2E95, 0x0504 }, { 0x366C, 0x0005 },
        };
        size_t i;
        for (i = 0; i < sizeof share18 / sizeof share18[0]; i++)
            if (share18[i][0] == vid && share18[i][1] == pid) p->share_offset = 18;
    }
    if (vid == 0x045E && pid == 0x02E3) p->elite1 = 1;
    if (vid == 0x045E && pid == 0x0B00)
        p->elite2 = bcd < 0x0500 ? 2 : bcd < 0x050B ? 3 : 4;
}

static int gip_put(xi_pad *p, uint8_t *out, int max, const uint8_t *pkt, int n)
{
    if (n > max) return 0;
    memcpy(out, pkt, (size_t)n);
    out[2] = p->out_seq++;
    if (!p->out_seq) p->out_seq = 1;
    return n;
}

int xi_gip_init_packet(xi_pad *p, int i, uint8_t *out, int max)
{
    /* As xpad sends them (xboxone_init_packets), for the pads it applies to. */
    static const uint8_t power_on[]  = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0, 1, 0x00 };
    static const uint8_t s_init[]    = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0, 0x0f, 0x06 };
    static const uint8_t elite2_ext[] = { 0x4d, 0x10, 0x01, 0x02, 0x07, 0x00 };
    static const uint8_t led_on[]    = { GIP_CMD_LED, GIP_OPT_INTERNAL, 0, 3, 0x00, 0x01, 0x14 };
    static const uint8_t auth_done[] = { GIP_CMD_AUTHENTICATE, GIP_OPT_INTERNAL, 0, 2, 0x01, 0x00 };
    static const uint8_t hori_ack[]  = { GIP_CMD_ACK, GIP_OPT_INTERNAL, 0, 9,
                                         0x00, 0x04, GIP_OPT_INTERNAL, 0x3a, 0x00, 0x00, 0x00, 0x80, 0x00 };
    static const uint8_t rumble_begin[] = { GIP_CMD_RUMBLE, 0x00, 0, 9, 0x00, 0x0F, 0x00, 0x00, 0x1D, 0x1D, 0xFF, 0x00, 0x00 };
    static const uint8_t rumble_end[]   = { GIP_CMD_RUMBLE, 0x00, 0, 9, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    const uint8_t *list[10];
    int lens[10], n = 0;
    int ms_s = p->vid == 0x045E && (p->pid == 0x02EA || p->pid == 0x0B00);
    int hori = (p->vid == 0x0E6F && p->pid == 0x0165) || (p->vid == 0x0F0D && p->pid == 0x0067);
    int powera = p->vid == 0x24C6 && (p->pid == 0x541A || p->pid == 0x542A || p->pid == 0x543A);

    if (p->kind != XI_GIP) return 0;
#define ADD(a) do { list[n] = (a); lens[n] = (int)sizeof(a); n++; } while (0)
    if (hori) ADD(hori_ack);
    ADD(power_on);
    if (ms_s) ADD(s_init);
    if (p->elite2) ADD(elite2_ext);
    ADD(led_on);
    ADD(auth_done);
    if (powera) { ADD(rumble_begin); ADD(rumble_end); }
#undef ADD
    if (i < 0 || i >= n) return 0;
    if (list[i] == elite2_ext) {            /* sent as is, sequence included */
        if (lens[i] > max) return 0;
        memcpy(out, list[i], (size_t)lens[i]);
        return lens[i];
    }
    return gip_put(p, out, max, list[i], lens[i]);
}

static int gip_ack(const uint8_t *d, uint8_t *reply, int *reply_len)
{
    /* The pad's own header back in the payload: cmd, options, length. */
    static const uint8_t ack[] = { GIP_CMD_ACK, GIP_OPT_INTERNAL, 0, 9,
                                   0x00, 0, GIP_OPT_INTERNAL, 0, 0x00, 0x00, 0x00, 0x00, 0x00 };
    memcpy(reply, ack, sizeof ack);
    reply[2] = d[2];                        /* the sequence being acknowledged */
    reply[5] = d[0];
    reply[7] = d[3];
    *reply_len = (int)sizeof ack;
    return 1;
}

static int parse_gip(xi_pad *p, const uint8_t *d, int n, pad_state *st,
                     uint8_t *reply, int *reply_len, int *reinit)
{
    uint32_t b = 0;
    uint8_t extra = 0;

    if (n < 4) return 0;

    if (d[0] == GIP_CMD_VIRTUAL_KEY) {      /* the Xbox button */
        if (n < 5) return 0;
        if (d[1] & GIP_OPT_ACK) gip_ack(d, reply, reply_len);
        p->guide = (d[4] & 0x03) ? 1 : 0;
        st->buttons = (st->buttons & ~(uint32_t)PAD_PS) | (p->guide ? PAD_PS : 0);
        return 1;
    }
    if (d[0] == GIP_CMD_ANNOUNCE) {         /* (re)announced: it wants the set-up again */
        if (d[1] & GIP_OPT_ACK) gip_ack(d, reply, reply_len);
        *reinit = 1;
        return 0;
    }
    if (d[0] == GIP_CMD_FIRMWARE) {         /* Elite 2 fw >= 5.11: paddles come here */
        if (p->elite2 == 4 && n >= 20) {
            uint8_t pb = d[19] ? 0 : d[18];     /* a custom on-pad profile already mapped them */
            p->paddles = (uint8_t)(((pb & 1) ? PAD_X_P1 : 0) | ((pb & 2) ? PAD_X_P2 : 0) |
                                   ((pb & 4) ? PAD_X_P3 : 0) | ((pb & 8) ? PAD_X_P4 : 0));
            st->extra = p->paddles;
            return 1;
        }
        return 0;
    }
    if (d[0] != GIP_CMD_INPUT || n < 18) {
        if (d[1] & GIP_OPT_ACK) gip_ack(d, reply, reply_len);
        return 0;
    }

    p->reports++;
    if (d[4] & 0x04) b |= PAD_OPTIONS;      /* Menu */
    if (d[4] & 0x08) b |= PAD_TOUCHPAD;     /* View: the "select" of PS games */
    if (d[4] & 0x10) b |= PAD_CROSS;
    if (d[4] & 0x20) b |= PAD_CIRCLE;
    if (d[4] & 0x40) b |= PAD_SQUARE;
    if (d[4] & 0x80) b |= PAD_TRIANGLE;
    if (d[5] & 0x01) b |= PAD_UP;
    if (d[5] & 0x02) b |= PAD_DOWN;
    if (d[5] & 0x04) b |= PAD_LEFT;
    if (d[5] & 0x08) b |= PAD_RIGHT;
    if (d[5] & 0x10) b |= PAD_L1;
    if (d[5] & 0x20) b |= PAD_R1;
    if (d[5] & 0x40) b |= PAD_L3;
    if (d[5] & 0x80) b |= PAD_R3;
    if (p->share_offset && n >= p->share_offset && (d[n - p->share_offset] & 0x01))
        b |= PAD_CREATE;
    if (p->guide) b |= PAD_PS;

    {
        unsigned lt = u16le(d + 6), rt = u16le(d + 8);
        if (lt > 1023) lt = 1023;
        if (rt > 1023) rt = 1023;
        st->l2 = (uint8_t)((lt * 255 + 511) / 1023);
        st->r2 = (uint8_t)((rt * 255 + 511) / 1023);
    }
    st->lx = axis_x(s16le(d + 10));
    st->ly = axis_y(s16le(d + 12));
    st->rx = axis_x(s16le(d + 14));
    st->ry = axis_y(s16le(d + 16));
    if (st->l2 > TRIGGER_PRESS) b |= PAD_L2;
    if (st->r2 > TRIGGER_PRESS) b |= PAD_R2;

    /* Paddles, as xpad reads them; muted when an on-pad profile is active. */
    if (p->elite1 && n >= 33) {
        uint8_t pb = memcmp(d + 4, d + 18, 2) != 0 ? 0 : d[32];
        extra = (uint8_t)(((pb & 2) ? PAD_X_P1 : 0) | ((pb & 8) ? PAD_X_P2 : 0) |
                          ((pb & 1) ? PAD_X_P3 : 0) | ((pb & 4) ? PAD_X_P4 : 0));
    } else if (p->elite2 == 2 && n >= 20) {
        uint8_t pb = d[19] ? 0 : d[18];
        extra = (uint8_t)(((pb & 1) ? PAD_X_P1 : 0) | ((pb & 2) ? PAD_X_P2 : 0) |
                          ((pb & 4) ? PAD_X_P3 : 0) | ((pb & 8) ? PAD_X_P4 : 0));
    } else if (p->elite2 == 3 && n >= 24) {
        uint8_t pb = d[23] ? 0 : d[22];
        extra = (uint8_t)(((pb & 1) ? PAD_X_P1 : 0) | ((pb & 2) ? PAD_X_P2 : 0) |
                          ((pb & 4) ? PAD_X_P3 : 0) | ((pb & 8) ? PAD_X_P4 : 0));
    } else if (p->elite2 == 4) {
        extra = p->paddles;
    }

    st->buttons = b;
    st->extra = extra;
    if (d[1] & GIP_OPT_ACK) gip_ack(d, reply, reply_len);
    return 1;
}

static int parse_x360(xi_pad *p, const uint8_t *d, int n, pad_state *st)
{
    uint32_t b = 0;

    if (n < 14 || d[0] != 0x00 || d[1] < 0x14) return 0;   /* LED / rumble status reports */
    p->reports++;
    if (d[2] & 0x01) b |= PAD_UP;
    if (d[2] & 0x02) b |= PAD_DOWN;
    if (d[2] & 0x04) b |= PAD_LEFT;
    if (d[2] & 0x08) b |= PAD_RIGHT;
    if (d[2] & 0x10) b |= PAD_OPTIONS;      /* Start */
    if (d[2] & 0x20) b |= PAD_TOUCHPAD;     /* Back */
    if (d[2] & 0x40) b |= PAD_L3;
    if (d[2] & 0x80) b |= PAD_R3;
    if (d[3] & 0x01) b |= PAD_L1;
    if (d[3] & 0x02) b |= PAD_R1;
    if (d[3] & 0x04) b |= PAD_PS;           /* Guide */
    if (d[3] & 0x10) b |= PAD_CROSS;
    if (d[3] & 0x20) b |= PAD_CIRCLE;
    if (d[3] & 0x40) b |= PAD_SQUARE;
    if (d[3] & 0x80) b |= PAD_TRIANGLE;
    st->l2 = d[4];
    st->r2 = d[5];
    st->lx = axis_x(s16le(d + 6));
    st->ly = axis_y(s16le(d + 8));
    st->rx = axis_x(s16le(d + 10));
    st->ry = axis_y(s16le(d + 12));
    if (st->l2 > TRIGGER_PRESS) b |= PAD_L2;
    if (st->r2 > TRIGGER_PRESS) b |= PAD_R2;
    st->buttons = b;
    st->extra = 0;
    return 1;
}

int xi_parse(xi_pad *p, const uint8_t *d, int n, pad_state *st,
             uint8_t *reply, int *reply_len, int *reinit)
{
    *reply_len = 0;
    *reinit = 0;
    if (!d || n <= 0) return 0;
    if (p->kind == XI_GIP) return parse_gip(p, d, n, st, reply, reply_len, reinit);
    if (p->kind == XI_X360) return parse_x360(p, d, n, st);
    return 0;
}

int xi_rumble_packet(xi_pad *p, uint8_t strong, uint8_t weak, uint8_t *out, int max)
{
    if (p->kind == XI_GIP) {
        uint8_t pkt[13] = { GIP_CMD_RUMBLE, 0x00, 0, 9, 0x00, 0x0F, 0x00, 0x00,
                            0, 0, 0xFF, 0x00, 0xFF };
        pkt[8] = (uint8_t)(strong * 100 / 255);
        pkt[9] = (uint8_t)(weak * 100 / 255);
        return gip_put(p, out, max, pkt, (int)sizeof pkt);
    }
    if (p->kind == XI_X360) {
        if (max < 8) return 0;
        memset(out, 0, 8);
        out[1] = 0x08;
        out[3] = strong;
        out[4] = weak;
        return 8;
    }
    return 0;
}

int xi_x360_led_packet(int player, uint8_t *out, int max)
{
    if (max < 3 || player < 1 || player > 4) return 0;
    out[0] = 0x01;
    out[1] = 0x03;
    out[2] = (uint8_t)(0x05 + player);      /* 6..9: player 1..4 lit */
    return 3;
}

const char *xi_model_name(uint16_t vid, uint16_t pid, int kind)
{
    static const struct { uint16_t vid, pid; const char *name; } k[] = {
        { 0x045E, 0x02D1, "Xbox One Controller" },
        { 0x045E, 0x02DD, "Xbox One Controller" },
        { 0x045E, 0x02E3, "Xbox One Elite" },
        { 0x045E, 0x02EA, "Xbox One S Controller" },
        { 0x045E, 0x0B00, "Xbox Elite Series 2" },
        { 0x045E, 0x0B0A, "Xbox Adaptive Controller" },
        { 0x045E, 0x0B12, "Xbox Series X|S Controller" },
        { 0x045E, 0x028E, "Xbox 360 Controller" },
        { 0x1532, 0x0A03, "Razer Wildcat" },
        { 0x1532, 0x0A14, "Razer Wolverine Ultimate" },
        { 0x1532, 0x0A29, "Razer Wolverine V2" },
        { 0x1532, 0x0A57, "Razer Wolverine V3 Pro" },
        { 0x1532, 0x0A59, "Razer Wolverine V3 Pro (dongle)" },
        { 0x3537, 0x1004, "GameSir T4 Kaleid" },
        { 0x3537, 0x100F, "GameSir Nova 2 Lite" },
        { 0x3537, 0x1010, "GameSir G7 SE" },
        { 0x3537, 0x1082, "GameSir G7 SE (HID)" },
    };
    size_t i;

    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i].vid == vid && k[i].pid == pid) return k[i].name;
    if (vid == 0x1532) return "Razer controller (wired)";
    if (vid == 0x3537 || vid == 0x2F24) return "GameSir controller (wired)";
    return kind == XI_X360 ? "Xbox 360-type controller (wired)" : "Xbox controller (wired)";
}
