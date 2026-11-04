# Master Control: credits

Master Control is free software under the GNU General Public License v3.0 or
later (see LICENSE). It is built from these GPL-3.0-or-later projects; every
author below keeps their credit, and the full source is published with any
release.

| Project | Author | What Master Control uses |
| --- | --- | --- |
| AnyPad-PS5 | its original author (see NOTICE) | the base PadBridge was built on |
| PadBridge PS5 / EeveeLution 0.3.16 | X-F1REBALL-X | the whole payload: Bluetooth (classic + LE), Xbox Series X\|S over BLE, GIP/XInput/HID wired drivers, per-user virtual pads, remapping, the web menu, rest-mode handling |
| ps5-wireless-pads | AngelLopezTorres | rest-mode pause/resume and taking over from a running copy |
| ShadowMountPlus | drakmor | the rest-mode and takeover patterns ps5-wireless-pads follows |
| Ghost-Control (PS5 USB Controller Patcher) | StonedModder — https://github.com/StonedModder/Ghostcontrol-PS5-USB-Controller-Patcher | the Switch Pro USB handshake and start-up subcommands; the DS4-family / HORI / XIM USB list and report layout (src/gc_usb.c) |
| Ghost-Control Manba V2 NBJr USB patch | the fork's author (credited in ghostcontrol-reference/CREDITS.md) | Manba V2 Switch-mode and receiver idle-mode (1a34:f517) handling, the Switch-mode axis correction this build checks against |
| PS5 Payload SDK | John Törnblom | the toolchain and libraries every payload here is built with |
| Kenney input prompts | Kenney (CC0) | button symbols in the classic menu (/classic) |
| Inter (fonts) | The Inter Project Authors, https://github.com/rsms/inter — SIL Open Font License 1.1 (web/fonts/OFL-Inter.txt) | body text of the Master Control menu, Latin subset, embedded in the page |
| Rajdhani (fonts) | Indian Type Foundry — SIL Open Font License 1.1 (web/fonts/OFL-Rajdhani.txt) | headings and the reactor labels, Latin subset, embedded in the page |

The Master Control menu's design (the reactor dial) and the tile icon (assets/icon0.png) are original to this project.

Not taken from Ghost-Control in this stage: the handover from the official
DualSense, which attaches to and writes into system processes.
