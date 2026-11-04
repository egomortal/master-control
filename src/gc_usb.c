/* SPDX-License-Identifier: GPL-3.0-or-later
 * Master Control: USB drivers ported from Ghost-Control (StonedModder) and
 * its Manba V2 NBJr fork. See gc_usb.h for what is taken and credits. */
#include "gc_usb.h"

#include <string.h>

static const uint16_t k_ds4_family[][2] = {
    { 0x0F0D, 0x0066 },     /* HORIPAD FPS Plus / Fighting Commander 4 (XIM4 default) */
    { 0x0F0D, 0x008F },     /* HORIPAD 4 */
    { 0x0F0D, 0x00EE },     /* HORIPAD FPS Plus (later id) */
    { 0x0F0D, 0x00C9 },     /* HORIPAD mini 4 */
    { 0x0F0D, 0x00F6 },     /* HORI Onyx */
};

static const uint16_t k_sony_ds4[][2] = {
    { 0x054C, 0x05C4 },     /* DualShock 4 v1 */
    { 0x054C, 0x09CC },     /* DualShock 4 v2 */
};

#define N(t) (int)(sizeof t / sizeof t[0])

static int in_list(const uint16_t (*t)[2], int n, uint16_t vid, uint16_t pid)
{
    int i;
    for (i = 0; i < n; i++) if (t[i][0] == vid && t[i][1] == pid) return 1;
    return 0;
}

int gcu_kind(uint16_t vid, uint16_t pid, int sony_ds4_ok)
{
    if (vid == 0x1A34 && pid == 0xF517) return GCU_IGNORE;      /* Manba receiver idle/update */
    if (vid == 0x057E && pid == 0x2009) return GCU_SWITCH;      /* Switch Pro, 8BitDo, Manba in Switch mode */
    if (in_list(k_ds4_family, N(k_ds4_family), vid, pid)) return GCU_DS4;
    if (sony_ds4_ok && in_list(k_sony_ds4, N(k_sony_ds4), vid, pid)) return GCU_DS4;
    return GCU_NONE;
}

const char *gcu_name(uint16_t vid, uint16_t pid)
{
    if (vid == 0x057E && pid == 0x2009) return "Switch Pro (USB)";
    if (vid == 0x0F0D) {
        switch (pid) {
        case 0x0066: return "HORIPAD FPS Plus / XIM (USB)";
        case 0x008F: return "HORIPAD 4 (USB)";
        case 0x00EE: return "HORIPAD FPS Plus (USB)";
        case 0x00C9: return "HORIPAD mini 4 (USB)";
        case 0x00F6: return "HORI Onyx (USB)";
        }
    }
    if (vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC)) return "DualShock 4 (USB)";
    return NULL;
}

/* ---- Switch Pro USB handshake (from Ghost-Control's controller_nintendo.c) ---- */

static int put(uint8_t out[][GCU_PKT], int *lens, int max, int k, const uint8_t *p, int n)
{
    if (k >= max || n > GCU_PKT) return k;
    memcpy(out[k], p, (size_t)n);
    lens[k] = n;
    return k + 1;
}

static int subcmd(gcu_switch *s, uint8_t id, uint8_t arg, uint8_t out[][GCU_PKT], int *lens, int max, int k)
{
    static const uint8_t neutral[8] = { 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40 };
    uint8_t b[12];

    b[0] = 0x01;
    b[1] = s->seq & 0x0F;
    memcpy(b + 2, neutral, 8);
    b[10] = id;
    b[11] = arg;
    s->seq = (uint8_t)((s->seq + 1) & 0x0F);
    return put(out, lens, max, k, b, 12);
}

int gcu_switch_begin(gcu_switch *s, int player, uint8_t out[][GCU_PKT], int *lens, int max)
{
    static const uint8_t hs2[2] = { 0x80, 0x02 }, hs4[2] = { 0x80, 0x04 };
    int k = 0;

    memset(s, 0, sizeof *s);
    s->state = GCU_HS_WAIT_81_01;
    s->seq = 1;
    s->player = player < 1 ? 1 : player > 4 ? 4 : player;
    k = put(out, lens, max, k, hs2, 2);         /* handshake */
    k = put(out, lens, max, k, hs4, 2);         /* USB only, no Bluetooth timeout */
    return k;
}

int gcu_switch_input(gcu_switch *s, const uint8_t *r, int n, uint8_t *tmp,
                     const uint8_t **norm, uint8_t out[][GCU_PKT], int *lens, int max)
{
    int k = 0;

    *norm = NULL;
    if (n < 1) return 0;
    switch (r[0]) {
    case 0x00:
        if (n < 12 || r[1] == 0) return 0;      /* all-zero artifact */
        memcpy(tmp, r, (size_t)(n > GCU_PKT ? GCU_PKT : n));
        tmp[0] = 0x30;                          /* clones: 0x30 layout under id 0 */
        *norm = tmp;
        s->state = GCU_HS_STREAMING;
        return 0;
    case 0x30:
        if (n < 12) return 0;
        s->state = GCU_HS_STREAMING;
        *norm = r;
        return 0;
    case 0x3F:
        *norm = r;
        return 0;
    case 0x21:                                  /* subcommand reply: also carries input */
        if (s->state == GCU_HS_STREAMING && n >= 12) *norm = r;
        return 0;
    case 0x81:
        if (n < 2) return 0;
        if (s->state == GCU_HS_STREAMING) {     /* the pad started over */
            s->state = GCU_HS_WAIT_81_01;
            s->reconnects++;
            return 0;
        }
        if (r[1] == 0x01 && s->state == GCU_HS_WAIT_81_01) {
            static const uint8_t hs2[2] = { 0x80, 0x02 };
            k = put(out, lens, max, k, hs2, 2);
            s->state = GCU_HS_WAIT_81_02;
        } else if (r[1] == 0x02 && s->state <= GCU_HS_WAIT_81_02) {
            static const uint8_t hs4[2] = { 0x80, 0x04 };
            k = put(out, lens, max, k, hs4, 2);
            k = subcmd(s, 0x40, 0x01, out, lens, max, k);   /* IMU on */
            k = subcmd(s, 0x48, 0x01, out, lens, max, k);   /* vibration on */
            k = subcmd(s, 0x30, (uint8_t)(1u << (s->player - 1)), out, lens, max, k);  /* player LED */
            k = subcmd(s, 0x03, 0x30, out, lens, max, k);   /* full report mode */
            s->state = GCU_HS_STREAMING;
        }
        return k;
    default:
        return 0;
    }
}
