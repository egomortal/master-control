/* Host tests for the wired Xbox-family protocol half (src/xinput.c). */
#include "xinput.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void gip_input(uint8_t *d, int len)
{
    memset(d, 0, (size_t)len);
    d[0] = 0x20; d[1] = 0x00; d[2] = 0x10; d[3] = (uint8_t)(len - 4);
}

static void put16(uint8_t *p, int v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); }

int main(void)
{
    xi_pad p;
    pad_state st;
    uint8_t d[64], reply[64], out[64];
    int rl, re, n, i;

    printf("interface classes\n");
    CHECK(xi_kind_of_interface(0xFF, 0x47, 0xD0) == XI_GIP);
    CHECK(xi_kind_of_interface(0xFF, 0x5D, 0x01) == XI_X360);
    CHECK(xi_kind_of_interface(0xFF, 0x5D, 0x81) == XI_X360);
    CHECK(xi_kind_of_interface(0x03, 0x00, 0x00) == XI_NONE);
    CHECK(xi_kind_of_interface(0xFF, 0x5D, 0x03) == XI_NONE);  /* 360 headset/chatpad iface */

    printf("GIP: Series X|S, centred and buttons\n");
    xi_setup(&p, XI_GIP, 0x045E, 0x0B12, 0x0509);
    pad_state_reset(&st);
    gip_input(d, 48);                       /* Series reports are longer */
    CHECK(xi_parse(&p, d, 48, &st, reply, &rl, &re) == 1);
    CHECK(st.lx == 128 && st.ly == 128 && st.rx == 128 && st.ry == 128);
    CHECK(st.buttons == 0 && st.l2 == 0 && st.r2 == 0 && rl == 0);

    d[4] = 0x10 | 0x80 | 0x04 | 0x08;       /* A, Y, Menu, View */
    d[5] = 0x01 | 0x10 | 0x80;              /* up, LB, RS */
    d[48 - 26] = 0x01;                      /* Share */
    put16(d + 6, 1023); put16(d + 8, 10);
    put16(d + 10, 32767); put16(d + 12, 32767);   /* left stick right + up */
    put16(d + 14, -32768); put16(d + 16, -32768); /* right stick left + down */
    CHECK(xi_parse(&p, d, 48, &st, reply, &rl, &re) == 1);
    CHECK(st.buttons == (PAD_CROSS | PAD_TRIANGLE | PAD_OPTIONS | PAD_TOUCHPAD | PAD_UP |
                         PAD_L1 | PAD_R3 | PAD_CREATE | PAD_L2));
    CHECK(st.l2 == 255 && st.r2 == 2);
    CHECK(st.lx == 255 && st.ly == 0);
    CHECK(st.rx == 0 && st.ry == 255);

    printf("GIP: small negative/positive values stay near the centre\n");
    gip_input(d, 48);
    put16(d + 10, -1); put16(d + 12, 1);
    xi_parse(&p, d, 48, &st, reply, &rl, &re);
    CHECK(st.lx == 127 && st.ly == 127);

    printf("GIP: Xbox button packet, acked when asked\n");
    memset(d, 0, sizeof d);
    d[0] = 0x07; d[1] = 0x30; d[2] = 0x42; d[3] = 0x02; d[4] = 0x01; d[5] = 0x5B;
    CHECK(xi_parse(&p, d, 6, &st, reply, &rl, &re) == 1);
    CHECK(st.buttons & PAD_PS);
    CHECK(rl == 13 && reply[0] == 0x01 && reply[2] == 0x42 && reply[5] == 0x07 && reply[7] == 0x02);
    gip_input(d, 48);
    xi_parse(&p, d, 48, &st, reply, &rl, &re);
    CHECK(st.buttons & PAD_PS);             /* still held across input reports */
    memset(d, 0, sizeof d);
    d[0] = 0x07; d[1] = 0x20; d[2] = 0x43; d[3] = 0x02; d[4] = 0x00;
    xi_parse(&p, d, 6, &st, reply, &rl, &re);
    CHECK(!(st.buttons & PAD_PS) && rl == 0);

    printf("GIP: announce asks for the set-up again\n");
    memset(d, 0, sizeof d);
    d[0] = 0x02; d[1] = 0x20; d[2] = 0x01; d[3] = 0x1c;
    CHECK(xi_parse(&p, d, 32, &st, reply, &rl, &re) == 0 && re == 1);

    printf("GIP: init sequence\n");
    xi_setup(&p, XI_GIP, 0x045E, 0x0B12, 0x0509);
    n = xi_gip_init_packet(&p, 0, out, sizeof out);
    CHECK(n == 5 && out[0] == 0x05 && out[1] == 0x20 && out[3] == 1 && out[4] == 0);
    for (i = 1; xi_gip_init_packet(&p, i, out, sizeof out) > 0; i++) {}
    CHECK(i == 3);                          /* power on, LED on, auth done */
    xi_setup(&p, XI_GIP, 0x045E, 0x02EA, 0x0408);
    for (i = 0; xi_gip_init_packet(&p, i, out, sizeof out) > 0; i++) {}
    CHECK(i == 4);                          /* + the One S init */
    xi_setup(&p, XI_GIP, 0x24C6, 0x543A, 0x0100);
    for (i = 0; xi_gip_init_packet(&p, i, out, sizeof out) > 0; i++) {}
    CHECK(i == 5);                          /* + PowerA rumble kick */

    printf("GIP: a non-Microsoft pad has no Share unless listed\n");
    xi_setup(&p, XI_GIP, 0x3537, 0x1010, 0x0100);
    CHECK(p.share_offset == 0);
    gip_input(d, 48);
    d[48 - 18] = 0x01; d[48 - 26] = 0x01;
    xi_parse(&p, d, 48, &st, reply, &rl, &re);
    CHECK(!(st.buttons & PAD_CREATE));
    xi_setup(&p, XI_GIP, 0x2DC8, 0x200F, 0x0100);
    CHECK(p.share_offset == 18);

    printf("GIP: Elite Series 2 paddles (fw 5.x early, fw 5.11 split)\n");
    xi_setup(&p, XI_GIP, 0x045E, 0x0B00, 0x0505);
    CHECK(p.elite2 == 3);
    gip_input(d, 48);
    d[22] = 0x05;
    xi_parse(&p, d, 48, &st, reply, &rl, &re);
    CHECK(st.extra == (PAD_X_P1 | PAD_X_P3));
    d[23] = 1;                              /* custom profile active: muted */
    xi_parse(&p, d, 48, &st, reply, &rl, &re);
    CHECK(st.extra == 0);
    xi_setup(&p, XI_GIP, 0x045E, 0x0B00, 0x0511);
    CHECK(p.elite2 == 4);
    memset(d, 0, sizeof d);
    d[0] = 0x0C; d[3] = 0x10; d[18] = 0x0A;
    xi_parse(&p, d, 24, &st, reply, &rl, &re);
    CHECK(st.extra == (PAD_X_P2 | PAD_X_P4));
    gip_input(d, 48);
    xi_parse(&p, d, 48, &st, reply, &rl, &re);
    CHECK(st.extra == (PAD_X_P2 | PAD_X_P4));   /* kept across input reports */

    printf("X360: Razer Wolverine V3 Pro style report\n");
    xi_setup(&p, XI_X360, 0x1532, 0x0A57, 0x0100);
    pad_state_reset(&st);
    memset(d, 0, sizeof d);
    d[0] = 0x00; d[1] = 0x14;
    d[2] = 0x02 | 0x10 | 0x20;              /* down, start, back */
    d[3] = 0x02 | 0x04 | 0x20;              /* RB, guide, B */
    d[4] = 0x80; d[5] = 0x10;
    put16(d + 6, 0); put16(d + 8, 0); put16(d + 10, 16384); put16(d + 12, -16384);
    CHECK(xi_parse(&p, d, 20, &st, reply, &rl, &re) == 1);
    CHECK(st.buttons == (PAD_DOWN | PAD_OPTIONS | PAD_TOUCHPAD | PAD_R1 | PAD_PS | PAD_CIRCLE | PAD_L2));
    CHECK(st.l2 == 0x80 && st.r2 == 0x10);
    CHECK(st.lx == 128 && st.ly == 128 && st.rx == 192 && st.ry == 192);
    d[0] = 0x01; d[1] = 0x03;               /* LED status report: ignored */
    CHECK(xi_parse(&p, d, 3, &st, reply, &rl, &re) == 0);
    CHECK(xi_x360_led_packet(1, out, sizeof out) == 3 && out[2] == 0x06);

    printf("rumble packets\n");
    xi_setup(&p, XI_GIP, 0x045E, 0x0B12, 0);
    n = xi_rumble_packet(&p, 255, 0, out, sizeof out);
    CHECK(n == 13 && out[0] == 0x09 && out[8] == 100 && out[9] == 0);
    xi_setup(&p, XI_X360, 0x045E, 0x028E, 0);
    n = xi_rumble_packet(&p, 10, 20, out, sizeof out);
    CHECK(n == 8 && out[1] == 0x08 && out[3] == 10 && out[4] == 20);

    printf("names\n");
    CHECK(strcmp(xi_model_name(0x045E, 0x0B12, XI_GIP), "Xbox Series X|S Controller") == 0);
    CHECK(strstr(xi_model_name(0x1532, 0x1234, XI_GIP), "Razer") != NULL);
    CHECK(strstr(xi_model_name(0x3537, 0x9999, XI_X360), "GameSir") != NULL);

    printf("garbage never crashes\n");
    {
        unsigned seed = 1;
        int k;
        for (k = 0; k < 20000; k++) {
            int len = (int)(seed % 64);
            int j;
            for (j = 0; j < len; j++) { seed = seed * 1103515245u + 12345u; d[j] = (uint8_t)(seed >> 16); }
            xi_setup(&p, (k & 1) ? XI_GIP : XI_X360, 0x045E, (k & 2) ? 0x0B00 : 0x02E3, 0x0511);
            xi_parse(&p, d, len, &st, reply, &rl, &re);
            seed = seed * 1103515245u + 12345u;
        }
    }

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("all xinput checks passed\n");
    return 0;
}
