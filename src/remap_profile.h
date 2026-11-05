/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MC_REMAP_PROFILE_H
#define MC_REMAP_PROFILE_H
#include "remap.h"
#define REMAP_PROFILE_DIR "/data/mastercontrol/profiles"
/* Reads only root/app/profile.map. First line: device = VVVV_PPPP\n.
 * Remaining bytes are ordinary remap text. No writes or active-map changes.
 * Identifiers are 1..63 ASCII letters, digits, '-' or '_'. No symlinks.
 * Return 1, or 0 and a stable error: profile-id/missing/profile-file/profile-map.
 */
int remap_profile_load(const char *root, const char *app, const char *profile,
                       uint16_t *vid, uint16_t *pid, char text[REMAP_TEXT_MAX+1],
                       const char **error);
#endif
