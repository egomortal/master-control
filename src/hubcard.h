/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Master Control's side of the Omnibloom plug-in standard (Bloom Core API v1,
 * SUITE-PLUGIN-STANDARD v1.1):
 *
 *   - the port comes from our ID card, /data/hub/apps/master-control.json,
 *     written by Omnibloom Setup (never by us);
 *   - what we actually bound goes to /data/hub/runtime/master-control.json,
 *     rewritten on every start and removed on a clean exit;
 *   - Bloom Core delivers intents as POST /hub/intent with an X-Hub-Token
 *     header that must match /data/hub/token.
 *
 * No console calls in here: everything is plain file and string work, so the
 * host tests cover it whole. The JSON reader is small and strict (RFC 8259
 * values, depth-limited); it is only used on our own card, the runtime file
 * and Bloom Core's intent bodies.
 */
#ifndef MC_HUBCARD_H
#define MC_HUBCARD_H

#include <stddef.h>

#define HUB_DIR             "/data/hub"
#define HUB_APP_ID          "master-control"
#define HUB_CARD_PATH       HUB_DIR "/apps/" HUB_APP_ID ".json"
#define HUB_RUNTIME_DIR     HUB_DIR "/runtime"
#define HUB_RUNTIME_PATH    HUB_RUNTIME_DIR "/" HUB_APP_ID ".json"
#define HUB_TOKEN_PATH      HUB_DIR "/token"
#define HUB_LOG_DIR         HUB_DIR "/logs/" HUB_APP_ID

/* Without a card: the suite's assigned default (Setup gives us 8766 too).
 * 8095, the old PadBridge/OmniPad-PS5 port, is never a default. */
#define HUB_DEFAULT_PORT    8766

/* ---- JSON (strict, small) ------------------------------------------------ */

/* 1 if text[0..len) is exactly one valid JSON value (whitespace around it
 * allowed), nested at most 16 deep. */
int hj_valid(const char *text, size_t len);

/* Finds key `key` in the top-level object of `json` (or, with `obj` set, in
 * the object found under that top-level key first). Returns a pointer to the
 * start of the value inside json, or NULL. Only call on hj_valid text. */
const char *hj_find(const char *json, size_t len, const char *obj, const char *key);

/* The value at p as a string (unescaped, \u escapes to UTF-8, NULs refused).
 * Returns 1 on success, 0 if it is not a string or does not fit `max`. */
int hj_string(const char *p, char *out, size_t max);

/* The value at p as an integer within [lo, hi]. Returns 1 on success. */
int hj_int(const char *p, long lo, long hi, long *out);

/* The value at p is `true`. */
int hj_true(const char *p);

/* ---- the card -------------------------------------------------------------- */

enum {
    HUB_PORT_DEFAULT = 0,   /* no card (or no usable one): HUB_DEFAULT_PORT */
    HUB_PORT_CARD    = 1,   /* from the card */
    HUB_PORT_OVERRIDE = 2   /* from the override file (tests, manual) */
};

/* A port we may listen on: 1024..65535 and none of the suite's fixed ports
 * (8760 Bloom Core, 9021 the loader, 2121 FTP). */
int hubcard_port_ok(long port);

/* Reads the card at `path` and returns its port, or 0 with *why set when the
 * card is missing, unreadable, too large, not valid JSON, for another app,
 * or has no usable port. */
int hubcard_read_port(const char *path, const char **why);

/* The port to use: the override file if it holds a usable number, else the
 * card, else HUB_DEFAULT_PORT. *source gets a HUB_PORT_*; *why (may be NULL)
 * says why the card was not used. */
int hubcard_resolve_port(const char *override_path, const char *card_path,
                         int *source, const char **why);

/* ---- the runtime file ---------------------------------------------------- */

/* Writes {"hub_api":1,"id":...,"port":...,"pid":...,"started_at":...,
 * "version":...} to `path` through a temporary file and a rename, making its
 * folder (and parents) first. Returns 1 on success. */
int hubcard_runtime_write(const char *path, int port, long pid, long started_at,
                          const char *version);

/* Removes it. Returns 1 if it is gone afterwards. */
int hubcard_runtime_remove(const char *path);

/* mkdir -p. Returns 1 if the folder exists afterwards. */
int hubcard_mkdirs(const char *dir);

/* ---- intents --------------------------------------------------------------- */

/* 1 if `presented` equals the token in `token_path` (32 hex characters,
 * compared in constant time). A missing or malformed token file denies. */
int hubcard_token_ok(const char *token_path, const char *presented);

/* The intent's name from Bloom Core's body {"hub_api":1,"intent":...,
 * "args":{...},"from":...}. Returns 1 and fills out on success. */
int hubcard_intent_name(const char *body, size_t len, char *out, size_t max);

/* Whether autoload.txt (pldmgr's start list) mentions Master Control.
 * Returns 1 yes, 0 no, -1 unreadable. Used only to explain the move from
 * 8095 in the log. */
int hubcard_autoload_mentions(const char *autoload_path);

#endif
