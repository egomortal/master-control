/* Wired Xbox-family controllers over USB, the protocol half (no USB calls,
 * host-testable). Two protocols cover every wired "Xbox" pad, including
 * GameSir, Razer, PowerA, PDP, 8BitDo-for-Xbox and Microsoft's own:
 *
 *   GIP  (Xbox One / Series and licensed pads): interface class ff/47/d0.
 *        Needs a short power-on sequence; the Xbox button comes in its own
 *        packet that must be acknowledged.
 *   X360 (XInput, Xbox 360 era; still used by e.g. Razer Wolverine V3 Pro,
 *        GameSir T4 / Nova in wired XInput mode): interface class ff/5d/01
 *        (or protocol 0x81). Streams 20-byte reports with no set-up.
 *
 * Layouts follow the Linux xpad driver (drivers/input/joystick/xpad.c),
 * which is the most widely tested reference for both.
 *
 * Sticks are passed through raw (no dead zone): games apply their own, as
 * they do for a DualSense, and a second dead zone here would only make the
 * pad feel numb around the centre. */
#ifndef PADBRIDGE_XINPUT_H
#define PADBRIDGE_XINPUT_H

#include "pad.h"

#include <stdint.h>

enum { XI_NONE = 0, XI_GIP = 1, XI_X360 = 2 };

/* Which protocol an interface speaks, from its class triplet. */
int xi_kind_of_interface(uint8_t cls, uint8_t sub, uint8_t proto);

typedef struct {
    int kind;                       /* XI_GIP or XI_X360 */
    uint16_t vid, pid, bcd;
    int share_offset;               /* GIP: Share bit at data[len - share_offset], 0: none */
    int elite2;                     /* GIP: Elite Series 2 paddle layout, 0 none, 2 fw<5, 3 fw5.0-5.10, 4 fw>=5.11 */
    int elite1;                     /* GIP: original Elite (02e3) */
    uint8_t out_seq;                /* GIP: next outgoing sequence number */
    uint8_t guide;                  /* Xbox button held (GIP: from its own packet) */
    uint8_t paddles;                /* last paddle bits (Elite 2 fw >= 5.11 sends them apart) */
    uint32_t reports;               /* input reports parsed */
} xi_pad;

void xi_setup(xi_pad *p, int kind, uint16_t vid, uint16_t pid, uint16_t bcd);

/* GIP set-up packets to send, in order, after the endpoints are open.
 * Fills `out` (up to `max` bytes) with packet number `i`; returns its
 * length, or 0 when there are no more. Sequence numbers are filled in. */
int xi_gip_init_packet(xi_pad *p, int i, uint8_t *out, int max);

/* Parses one packet from the IN endpoint into `st`.
 * Returns 1 if `st` changed and should be sent to the console, 0 if not.
 * If the pad wants an answer (GIP acknowledgement, or a repeat of the
 * set-up after it announced itself again), it is written to `reply`
 * (*reply_len > 0); call xi_gip_init_packet again from 0 when
 * *reinit is set. */
int xi_parse(xi_pad *p, const uint8_t *d, int n, pad_state *st,
             uint8_t *reply, int *reply_len, int *reinit);

/* A rumble packet (strong = left/low motor, weak = right/high, 0..255).
 * Returns its length (0 if not possible). */
int xi_rumble_packet(xi_pad *p, uint8_t strong, uint8_t weak, uint8_t *out, int max);

/* X360 only: the ring LED for a player number 1-4 (0x06..0x09 pattern). */
int xi_x360_led_packet(int player, uint8_t *out, int max);

/* A readable model name, for notifications and the menu. */
const char *xi_model_name(uint16_t vid, uint16_t pid, int kind);

#endif
