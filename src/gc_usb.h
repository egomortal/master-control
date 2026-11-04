/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Master Control: USB controller drivers ported from Ghost-Control.
 *
 * Ghost-Control (PS5 USB controller patcher) by StonedModder,
 *   https://github.com/StonedModder/Ghostcontrol-PS5-USB-Controller-Patcher
 * and its Manba V2 NBJr fork (USB patch: Manba PC/XInput and Switch modes,
 * Switch-mode axis correction, receiver idle-mode handling).
 * Both GPL-3.0-or-later. What is taken here:
 *   - the Nintendo Switch Pro USB handshake ([80 02], [80 04], the 0x81
 *     reply state machine and the start-up subcommands), which pads in
 *     Switch mode (Switch Pro, 8BitDo, Manba) need before they send full
 *     reports over USB;
 *   - the DS4-family list: DualShock 4 and DS4-wire-format pads (HORIPAD
 *     FPS Plus / Fighting Commander 4, HORIPAD 4, HORIPAD mini 4, XIM4 which
 *     enumerates as a HORIPAD), whose short USB report 0x01 needs no init;
 *   - the Manba receiver idle/update mode (1a34:f517), which is not a pad
 *     and is left alone.
 * Not taken (Stage 1): Ghost-Control's handover from the official DualSense,
 * which attaches to and writes into system processes.
 *
 * The report parsers themselves are PadBridge's profiles (profiles.c): the
 * DS4 short report and the Switch 0x30/0x3F reports read the same as over
 * Bluetooth. This file only decides which pads take this path and drives the
 * Switch handshake. Pure functions: host-tested in tests/test_gc_usb.c. */
#ifndef MC_GC_USB_H
#define MC_GC_USB_H

#include <stdint.h>

enum {
    GCU_NONE = 0,       /* not ours: the existing PadBridge paths decide */
    GCU_DS4,            /* DS4 wire format, report 0x01, no init */
    GCU_SWITCH,         /* Switch Pro USB protocol: handshake, then 0x30 */
    GCU_IGNORE,         /* never a pad (Manba receiver idle mode) */
};

#define GCU_DS4_FLAG "/data/mastercontrol/ds4_usb"   /* opt-in: Sony DualShock 4 over USB */

/* Which driver takes vid:pid. `sony_ds4_ok`: the opt-in flag for genuine
 * Sony DualShock 4 pads (left to the console by default, as in PadBridge). */
int gcu_kind(uint16_t vid, uint16_t pid, int sony_ds4_ok);

/* A model name for the menu, or NULL. */
const char *gcu_name(uint16_t vid, uint16_t pid);

/* ---- Switch Pro USB handshake ---- */

#define GCU_HS_WAIT_81_01 0
#define GCU_HS_WAIT_81_02 1
#define GCU_HS_STREAMING  2

#define GCU_PKT 64

typedef struct {
    int state;
    uint8_t seq;            /* subcommand counter, 4 bits */
    int player;             /* 1..4, for the player LEDs */
    int reconnects;         /* 0x81 seen while streaming */
} gcu_switch;

/* Starts the handshake. Writes the packets to send right after the device
 * is opened ([80 02], [80 04], as Ghost-Control does) and returns how many. */
int gcu_switch_begin(gcu_switch *s, int player, uint8_t out[][GCU_PKT], int *lens, int max);

/* Feeds one input report. Writes packets to send (0 or more) and returns
 * how many. `*norm` is set to the report to hand to the parser (a 0x00
 * report, which some clones send, is rewritten as 0x30 into `tmp`), or NULL
 * if the report is handshake traffic only. */
int gcu_switch_input(gcu_switch *s, const uint8_t *r, int n, uint8_t *tmp,
                     const uint8_t **norm, uint8_t out[][GCU_PKT], int *lens, int max);

#endif
