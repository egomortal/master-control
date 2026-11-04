/* SPDX-License-Identifier: GPL-3.0-or-later
 * Master Control: the drivers ported from Ghost-Control (src/gc_usb.c), and
 * what their reports turn into through PadBridge's profiles. Report bytes
 * follow the layouts documented in gc_usb.h / profiles.c (Ghost-Control's
 * controller_nintendo.c and controller_ds4.c), not captures of hardware. */
#include "../src/gc_usb.h"
#include "../src/profiles.h"

#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void test_kind(void)
{
    printf("which pads take the Ghost-Control path\n");
    CHECK(gcu_kind(0x057E, 0x2009, 0) == GCU_SWITCH);
    CHECK(gcu_kind(0x0F0D, 0x0066, 0) == GCU_DS4);          /* HORIPAD FPS Plus / XIM4 */
    CHECK(gcu_kind(0x0F0D, 0x008F, 0) == GCU_DS4);
    CHECK(gcu_kind(0x0F0D, 0x00C9, 0) == GCU_DS4);
    CHECK(gcu_kind(0x0F0D, 0x00EE, 0) == GCU_DS4);
    CHECK(gcu_kind(0x054C, 0x09CC, 0) == GCU_NONE);         /* Sony DS4: opt-in only */
    CHECK(gcu_kind(0x054C, 0x09CC, 1) == GCU_DS4);
    CHECK(gcu_kind(0x054C, 0x05C4, 1) == GCU_DS4);
    CHECK(gcu_kind(0x054C, 0x0CE6, 1) == GCU_NONE);         /* DualSense: always the console's */
    CHECK(gcu_kind(0x1A34, 0xF517, 0) == GCU_IGNORE);       /* Manba receiver idle */
    CHECK(gcu_kind(0x045E, 0x028E, 0) == GCU_NONE);         /* XInput: PadBridge's own path */
    CHECK(gcu_kind(0x3537, 0x1082, 0) == GCU_NONE);         /* GameSir HID: unchanged */
    CHECK(gcu_kind(0x1532, 0x0A14, 0) == GCU_NONE);         /* Razer Wolverine: unchanged */
    CHECK(gcu_name(0x0F0D, 0x0066) && gcu_name(0x057E, 0x2009));
}

static void test_handshake(void)
{
    gcu_switch s;
    uint8_t out[8][GCU_PKT], tmp[GCU_PKT], r[64];
    int lens[8], n, i;
    const uint8_t *norm;

    printf("Switch Pro USB handshake\n");
    n = gcu_switch_begin(&s, 2, out, lens, 8);
    CHECK(n == 2);
    CHECK(lens[0] == 2 && out[0][0] == 0x80 && out[0][1] == 0x02);
    CHECK(lens[1] == 2 && out[1][0] == 0x80 && out[1][1] == 0x04);
    CHECK(s.state == GCU_HS_WAIT_81_01);

    memset(r, 0, sizeof r); r[0] = 0x81; r[1] = 0x01;
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(n == 1 && out[0][0] == 0x80 && out[0][1] == 0x02 && !norm);
    CHECK(s.state == GCU_HS_WAIT_81_02);

    r[1] = 0x02;
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(n == 5 && !norm);
    CHECK(out[0][0] == 0x80 && out[0][1] == 0x04);
    CHECK(out[1][0] == 0x01 && out[1][10] == 0x40 && out[1][11] == 0x01);   /* IMU on */
    CHECK(out[2][0] == 0x01 && out[2][10] == 0x48 && out[2][11] == 0x01);   /* vibration */
    CHECK(out[3][0] == 0x01 && out[3][10] == 0x30 && out[3][11] == 0x02);   /* player 2 LED */
    CHECK(out[4][0] == 0x01 && out[4][10] == 0x03 && out[4][11] == 0x30);   /* full reports */
    for (i = 1; i < 5; i++) CHECK(lens[i] == 12 && out[i][2] == 0x00 && out[i][3] == 0x01);  /* neutral rumble */
    CHECK(((out[1][1] + 1) & 0x0F) == out[2][1]);                          /* rolling counter */
    CHECK(s.state == GCU_HS_STREAMING);

    memset(r, 0, sizeof r); r[0] = 0x30;
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(n == 0 && norm == r);

    memset(r, 0, sizeof r); r[0] = 0x00;                    /* all-zero artifact */
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(n == 0 && !norm);
    r[1] = 0x91;                                            /* clone: 0x30 layout under id 0 */
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(norm == tmp && tmp[0] == 0x30 && tmp[1] == 0x91);

    memset(r, 0, sizeof r); r[0] = 0x81; r[1] = 0x01;       /* the pad started over */
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(n == 0 && s.state == GCU_HS_WAIT_81_01 && s.reconnects == 1);
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(n == 1 && s.state == GCU_HS_WAIT_81_02);

    /* An 0x81 0x02 straight away (some pads skip 0x01) still completes. */
    gcu_switch_begin(&s, 1, out, lens, 8);
    memset(r, 0, sizeof r); r[0] = 0x81; r[1] = 0x02;
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    CHECK(n == 5 && s.state == GCU_HS_STREAMING && out[3][11] == 0x01);

    /* Not enough room: never writes past `max`. */
    gcu_switch_begin(&s, 1, out, lens, 8);
    n = gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 3);
    CHECK(n == 3);
}

/* 12-bit stick packing of the Switch 0x30 report. */
static void pack_stick(uint8_t *p, int x, int y)
{
    p[0] = (uint8_t)(x & 0xFF);
    p[1] = (uint8_t)(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    p[2] = (uint8_t)(y >> 4);
}

static void test_switch_translate(void)
{
    const pad_profile *p = profile_find(0x057E, 0x2009);
    uint8_t ctx[PROFILE_CTX_MAX], r[64], tmp[GCU_PKT];
    pad_state st;
    gcu_switch s;
    uint8_t out[8][GCU_PKT];
    int lens[8];
    const uint8_t *norm;

    printf("Switch USB report -> console state (PadBridge's Switch profile)\n");
    CHECK(p != NULL);
    if (!p) return;
    memset(ctx, 0, sizeof ctx);
    memset(r, 0, sizeof r);
    r[0] = 0x30;
    r[2] = 0x91;                    /* battery: full, on cable */
    r[3] = 0x04 | 0x08;             /* B and A */
    r[4] = 0x10;                    /* Home */
    r[5] = 0x02 | 0x40;             /* d-pad up, L */
    pack_stick(r + 6, 2048, 2048 + 1600);   /* left stick pushed fully up */
    pack_stick(r + 9, 2048 + 1600, 2048);   /* right stick fully right */
    pad_state_reset(&st);
    CHECK(p->parse(ctx, r, 64, &st) == PARSE_FULL);
    CHECK(st.buttons & PAD_CROSS);  /* B: bottom face button */
    CHECK(st.buttons & PAD_CIRCLE); /* A: right face button */
    CHECK(st.buttons & PAD_PS);
    CHECK(st.buttons & PAD_UP);
    CHECK(st.buttons & PAD_L1);
    CHECK(st.lx >= 126 && st.lx <= 130);
    CHECK(st.ly < 10);              /* up is y towards 0 on the console: the Manba/Switch-mode Y fix */
    CHECK(st.rx > 245);
    CHECK(st.ry >= 126 && st.ry <= 130);

    /* The same report from a clone under id 0, through the handshake layer. */
    gcu_switch_begin(&s, 1, out, lens, 8);
    r[0] = 0x00;
    r[1] = 0x05;                    /* the 0x30 report's timer byte: non-zero in real traffic */
    gcu_switch_input(&s, r, 64, tmp, &norm, out, lens, 8);
    pad_state_reset(&st);
    CHECK(norm && p->parse(ctx, norm, 64, &st) == PARSE_FULL);
    CHECK((st.buttons & PAD_CROSS) && st.ly < 10);
}

static void test_ds4_translate(void)
{
    const pad_profile *p = profile_find(0x054C, 0x05C4);
    uint8_t ctx[PROFILE_CTX_MAX], r[27];
    pad_state st;

    printf("HORI / XIM / DS4 USB report 0x01 -> console state (PadBridge's DS4 profile)\n");
    CHECK(p != NULL);
    if (!p) return;
    memset(ctx, 0, sizeof ctx);
    memset(r, 0, sizeof r);         /* HORI pads truncate the report: 27 bytes */
    r[0] = 0x01;
    r[1] = 0;   r[2] = 255;         /* left stick: left, down */
    r[3] = 128; r[4] = 128;
    r[5] = 0x20 | 0x02;             /* Cross, hat E */
    r[6] = 0x01 | 0x20;             /* L1, Options */
    r[7] = 0x01;                    /* PS */
    r[8] = 200; r[9] = 10;          /* L2, R2 analog */
    pad_state_reset(&st);
    CHECK(p->parse(ctx, r, (int)sizeof r, &st) != PARSE_NONE);
    CHECK(st.lx == 0 && st.ly == 255);
    CHECK(st.buttons & PAD_CROSS);
    CHECK(st.buttons & PAD_RIGHT);
    CHECK(!(st.buttons & PAD_UP));
    CHECK(st.buttons & PAD_L1);
    CHECK(st.buttons & PAD_OPTIONS);
    CHECK(st.buttons & PAD_PS);
    CHECK(st.l2 == 200 && st.r2 == 10);
}

int main(void)
{
    test_kind();
    test_handshake();
    test_switch_translate();
    test_ds4_translate();
    if (g_fail) { printf("%d gc_usb checks FAILED\n", g_fail); return 1; }
    printf("all gc_usb checks passed\n");
    return 0;
}
