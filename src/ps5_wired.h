/* Wired Xbox-family controllers on the PS5's USB ports (low-latency patch).
 *
 * A thread of its own finds GIP (Xbox One / Series and licensed pads such as
 * GameSir and Razer) and XInput (Xbox 360-type) controllers on the USB
 * ports, claims their game-input interface, and turns every report into a
 * pad_state the moment it arrives: it sleeps in poll() on the devices, keeps
 * several reads queued so the controller is never kept waiting between two
 * reports, and never applies a dead zone or smoothing.
 *
 * The protocol itself is in xinput.c (host-tested). */
#ifndef PADBRIDGE_PS5_WIRED_H
#define PADBRIDGE_PS5_WIRED_H

#include "pad.h"

#include <stdint.h>

#define WIRED_MAX_PADS 4

typedef struct {
    /* `slot` is a virtual-pad slot (HOST_MAX_PADS and up). Called from the
     * wired thread. */
    void (*on_connect)(int slot, uint16_t vid, uint16_t pid, const char *name);
    void (*on_state)(int slot, uint16_t vid, uint16_t pid, const pad_state *st);
    void (*on_disconnect)(int slot);
} wired_events;

typedef struct {
    int slot;                   /* zero-based virtual-pad slot */
    uint16_t vid, pid;
    char name[48];
    char path[24];
    pad_state state;
} wired_pad_snapshot;

/* Starts the thread. `skip_node` is a ugen path never to touch (the
 * Bluetooth chip). Returns 0 if the thread could not start. */
int  wired_start(const wired_events *ev, const char *skip_node);

/* Rest mode: 1 lets go of every device and stops looking; 0 resumes. */
void wired_pause(int on);

/* Lets go of everything and ends the thread. */
void wired_stop(void);

/* How many wired controllers are in use, and a line about each for the menu. */
int  wired_count(void);
int  wired_describe(int i, char *out, int max);
/* A coherent copy for the web dashboard. `i` is the wired array index. */
int  wired_snapshot(int i, wired_pad_snapshot *out);

#endif
