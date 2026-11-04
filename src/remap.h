/* Button remapping per controller model (vid:pid), applied to every report
 * just before it reaches the virtual DualSense.
 *
 * A map says, for each DualSense button the profile produced and for each
 * extra button (back paddles, M-keys: pad_state.extra), which DualSense
 * button comes out, or none. Several sources may give the same target.
 * Options: swap the sticks, invert either stick's Y axis, swap L2/R2, and a
 * hair trigger per side.
 *
 * Order of application (remap_apply):
 *   1. swap_triggers: L2/R2 bits and analog values swap, as if the physical
 *      triggers were the other way round. The table below then sees them so.
 *   2. the button table. Analog trigger values follow the trigger buttons:
 *      the DualSense L2 value is the largest analog value of the physical
 *      triggers mapped to L2, or 255 if any other pressed button maps to L2
 *      (same for R2). A trigger mapped to a face button gives 0 analog.
 *   3. swap_sticks, then invert_ly / invert_ry (of the sticks as sent).
 *   4. hair_l2 / hair_r2: past REMAP_HAIR on the output trigger (or with its
 *      digital bit already set), full press: 255 and the digital bit; below
 *      it, released (0).
 * Bits outside the table pass through untouched. With no map, nothing
 * changes and extra buttons do nothing.
 *
 * Files: <dir>/VVVV_PPPP.map (upper-case hex ids), "key = value" lines, '#'
 * comments, written to a temporary file and renamed:
 *
 *   name = Xbox Wireless Controller     (informative)
 *   cross = circle                      source = target
 *   p1 = cross                          extra button = target
 *   l2 = none
 *   swap_sticks = 1                     options: 0/1 (also on/off, yes/no, true/false)
 *   invert_ly = 1  invert_ry = 0  swap_triggers = 0  hair_l2 = 0  hair_r2 = 1
 *
 *   sources: cross circle square triangle l1 r1 l2 r2 l3 r3 options create
 *            touchpad ps up down left right p1 p2 p3 p4 m5 m6
 *            (also: share = create, m1..m4 = p1..p4)
 *   targets: the 18 DualSense names above, or none
 * A source not listed keeps its default (itself; none for extra buttons).
 *
 * Threads: remap_apply may run on several threads at once, against one
 * writer at a time (saves, resets, remap_note_device are serialised by a
 * tiny spin lock that remap_apply never takes). A map in use is never
 * written: each model keeps a ring of compiled maps and a save fills the
 * next one, then publishes its index. */
#ifndef PH_REMAP_H
#define PH_REMAP_H

#include "pad.h"

#include <stddef.h>
#include <stdint.h>

#define REMAP_MODELS    16          /* controller models known at once */
#define REMAP_SRC_N     24          /* 18 DualSense buttons + 6 extra */
#define REMAP_BTN_N     18          /* the DualSense buttons, sources 0..17 */
#define REMAP_NONE      0xFF        /* target: nothing */
#define REMAP_HAIR      10          /* hair trigger threshold, 0..255 */
#define REMAP_TEXT_MAX  4096        /* longest map text accepted */
#define REMAP_NAME_MAX  48

/* A map as the user edits it. target[i] is a DualSense button index
 * (0..REMAP_BTN_N-1, same order as the sources) or REMAP_NONE. */
typedef struct {
    uint8_t target[REMAP_SRC_N];
    uint8_t swap_sticks, invert_ly, invert_ry, swap_triggers, hair_l2, hair_r2;
} remap_map;

/* Loads every saved map from `dir` (created if missing). NULL: no files.
 * Forgets all models and maps known before. Not thread-safe: call once at
 * start-up, before any remap_apply. */
void remap_init(const char *dir);

/* A controller model has been seen (Bluetooth or wired), so the menu can
 * list it even with no map. `name` may be NULL. Cheap; no file I/O. */
void remap_note_device(uint16_t vid, uint16_t pid, const char *name);

/* On every input report, before the virtual pad. No I/O, no allocation, no
 * lock; returns at once when the model has no map. */
void remap_apply(uint16_t vid, uint16_t pid, pad_state *st);

/* ---- the menu ------------------------------------------------------------ */

/* The default (identity) map. */
void remap_default(remap_map *m);

/* Parses map text (the file format) over *m, which should hold the default
 * first. Returns 1, or 0 with the first bad line's number in *bad_line and
 * *m in an unspecified state. A "name" line, if any, is copied to name
 * (may be NULL). */
int remap_parse(const char *text, remap_map *m, char *name, size_t name_max, int *bad_line);

/* Writes the map as file text: only what differs from the default, plus the
 * name if not empty. Returns the length, or -1 if it does not fit. */
int remap_format(const remap_map *m, const char *name, char *out, size_t max);

/* Validates, saves and applies `text` for vid:pid. Returns 1, or 0 with a
 * short reason in *err ("syntax", "full", "write"); on a syntax error the
 * line number is in *bad_line. Nothing changes on failure. */
int remap_set(uint16_t vid, uint16_t pid, const char *text, const char **err, int *bad_line);

/* Removes vid:pid's map (file and live). Returns 1 if there was one. */
int remap_reset(uint16_t vid, uint16_t pid);

/* Model i (0-based) of those seen or with a map. Returns 0 past the end. */
typedef struct {
    uint16_t vid, pid;
    char name[REMAP_NAME_MAX];
    int seen, saved;            /* saved: has a map (live and on disk) */
    remap_map map;              /* the default if none */
} remap_info;
int remap_get(int i, remap_info *out);

/* Names of sources and targets: index 0..REMAP_SRC_N-1 (targets use the
 * first REMAP_BTN_N); NULL past the end. */
const char *remap_source_name(int i);

#endif
