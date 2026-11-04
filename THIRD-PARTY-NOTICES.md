# Third-party notices

Master Control is licensed under the GNU General Public License v3.0 or later ([LICENSE](LICENSE)).
It is built from the GPL-3.0-or-later projects below. Every author keeps their copyright and their
credit, and the complete source is in this repository and attached to every release.

## Code

| Project | Author | Licence | What Master Control uses |
| --- | --- | --- | --- |
| AnyPad-PS5 | elmonomalvad0 (also published as *sinfiltros*) | GPL-3.0-or-later | The original base: the Bluetooth host over the PS5's internal chip, controller profiles, virtual DualSense output. The author asks, not as a condition, that forks mention elmonomalvad0 and link the project. |
| PadBridge PS5, including 0.3.16 "EeveeLution" | X-F1REBALL-X, <https://github.com/X-F1REBALL-X/PadBridge-PS5> | GPL-3.0-or-later | The whole payload Master Control starts from: Bluetooth Classic and LE, Xbox Series X\|S over BLE, the GIP / XInput / HID wired drivers, per-user virtual pads, remapping, the web menu, the dashboard tile |
| ps5-wireless-pads | AngelLopezTorres | GPL-3.0-or-later | Pausing for rest mode and resuming after it; taking over from a running copy |
| Ghost-Control (PS5 USB Controller Patcher) | StonedModder, <https://github.com/StonedModder/Ghostcontrol-PS5-USB-Controller-Patcher> | GPL-3.0-or-later | The Switch Pro USB handshake and start-up subcommands; the DualShock 4-family, HORI and XIM USB list and report layout (`src/gc_usb.c`, `src/gc_usb.h`) |
| Ghost-Control Manba V2 NBJr USB patch | the fork's author | GPL-3.0-or-later | Manba V2 Switch-mode and receiver idle-mode (`1a34:f517`) handling, and the Switch-mode axis check |

## Ideas and references

These shaped the code through AnyPad-PS5 and PadBridge; no code is copied from them directly here.

- **ShadowMountPlus** (drakmor): the rest-mode and takeover patterns.
- **FGG-PlayPods** (FGGstore): sharing the PS5's own Bluetooth chip with the system.
- **MissionControl** (ndeadly): controller profiles by vendor and model, and many controllers' report layouts.

## Fonts (embedded in the hub page)

| Font | Author | Licence | Licence text |
| --- | --- | --- | --- |
| Inter (Latin subset) | The Inter Project Authors, <https://github.com/rsms/inter> | SIL Open Font License 1.1 | [web/fonts/OFL-Inter.txt](web/fonts/OFL-Inter.txt) |
| Rajdhani (Latin subset) | Indian Type Foundry | SIL Open Font License 1.1 | [web/fonts/OFL-Rajdhani.txt](web/fonts/OFL-Rajdhani.txt) |

## Art

| Art | Licence | Where |
| --- | --- | --- |
| Kenney Input Prompts 1.5 (button symbols in the `/classic` menu) | CC0 1.0 (Kenney, kenney.nl) | `assets/kenney-input-prompts/`, `third_party/kenney-input-prompts/` |
| Reactor dial art and the tile icon | Original to this project | `web/art/reactor-1024.webp`, `assets/icon0.png` |

## Toolchain and system libraries

- **PS5 Payload SDK** (John Törnblom, <https://github.com/ps5-payload-dev/sdk>, GPL-3.0): the compiler wrappers, start-up code and link stubs used to build the ELF. Release builds use the version pinned in `.github/workflows/release.yml`.
- At run time the ELF loads the console's own system libraries from the console: `libkernel_web`, `libSceLibcInternal`, `libSceNet`, `libScePad`, `libSceUserService`, `libSceSystemService`, `libSceAppInstUtil` and `libSceMbus`. **None of them are included in this repository or in the ELF.**

Master Control does not include or redistribute any Sony SDK, firmware, keys or system files.
