<p align="center"><img src="assets/icon0.png" width="160" alt="Master Control icon: a chrome reactor dial with a glowing blue core"></p>

<h1 align="center">Master Control</h1>

<p align="center"><b>Use Xbox, Switch, GameSir, Razer and other controllers on a jailbroken PS5.</b><br>
<sub>Bluetooth through the console's own chip, or plugged in by USB. No dongle.</sub></p>

<p align="center">
  <img alt="License: GPL-3.0-or-later" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue">
  <img alt="Platform: PS5 firmware 13.60" src="https://img.shields.io/badge/PS5-FW%2013.60-003791">
  <img alt="Status: beta" src="https://img.shields.io/badge/status-beta-orange">
</p>

Master Control is a small program (an ELF payload) that runs in the background on a jailbroken
PS5. It connects controllers the PS5 doesn't normally accept, and the console and your games see
each one as a DualSense. It comes with its own menu, the **reactor hub**, which you use from the
TV with a controller.

It's the controller half of [Omnibloom](https://github.com/egomortal/omnibloom), and it works on its own too.

> Not affiliated with OmniPad-PS5, Sony Interactive Entertainment, Microsoft, Nintendo or any controller maker.

## What it does

- **Bluetooth pads**: pair Xbox, Switch Pro, DualShock 4 and other Bluetooth controllers with the PS5's built-in Bluetooth.
- **USB pads**: plug in Xbox-style, GameSir, Razer, Switch-mode, 8BitDo, HORI and similar wired pads.
- **Up to four players**, each controller a separate player that the PS5 can give to a different account.
- **Button remapping** per controller model, which works everywhere: the system menus and every game.
- **The reactor hub**: see what's connected and its battery, pair, remap, and send a PS-button press, all from the couch.
- **Rest mode**: it pauses before the console sleeps and comes back by itself when it wakes. Paired controllers reconnect.
- **Updating** is just sending the new ELF: it asks the running copy to stop and takes its place.

It doesn't change games, the firmware or the jailbreak.

## Controllers

Only three controllers have been tried on a PS5 on firmware 13.60 so far. A 🧪 row means the
code is written and passes the simulator tests, but nobody has tried that controller on a console
with Master Control yet. Reports are very welcome.

| Symbol | Meaning |
| :---: | --- |
| ✅ | Works: tested on a real PS5 on firmware 13.60 |
| ☑️ | Worked in the projects Master Control is built from, on an older firmware (10.20), not yet re-tested on 13.60 |
| 🧪 | Experimental: supported in code, not tested on a console |

| Controller | How | Status | Comes from |
| --- | --- | :---: | --- |
| Xbox Series X\|S (`045e:0b13`) | Bluetooth LE | ✅ | PadBridge |
| GameSir G7 SE (`3537:1082`, HID mode) | USB | ✅ | PadBridge |
| Razer Wolverine Ultimate (`1532:0a14`) | USB | ✅ | PadBridge |
| Sony DualShock 4 v1 / v2 (`054c:05c4`, `054c:09cc`) | Bluetooth | ☑️ | PadBridge / AnyPad |
| Other Xbox One / Elite 2 / Adaptive models | Bluetooth (Classic or LE) | 🧪 | PadBridge |
| Xbox One, Series and 360 pads | USB (GIP / XInput) | 🧪 | PadBridge |
| Sony DualSense, DualSense Edge | Bluetooth | 🧪 | PadBridge |
| Switch Pro Controller and Switch Online pads | Bluetooth | 🧪 | PadBridge |
| Switch Pro Controller (`057e:2009`) | USB | 🧪 | Ghost-Control |
| 8BitDo and Manba V2 pads in Switch mode | USB | 🧪 | Ghost-Control |
| HORIPAD 4, HORIPAD FPS Plus, HORIPAD mini 4, HORI Onyx, Fighting Commander 4 | USB | 🧪 | Ghost-Control |
| XIM4 (shows up as a HORIPAD FPS Plus) | USB | 🧪 | Ghost-Control |
| Sony DualShock 4 | USB (off by default, see below) | 🧪 | Ghost-Control |
| 8BitDo, other GameSir models, SteelSeries, PowerA MOGA, NVIDIA Shield, Razer Serval and about 40 more | Bluetooth / USB | 🧪 | PadBridge |
| Any other Bluetooth HID gamepad | Bluetooth, generic profile | 🧪 | PadBridge |

Every controller ID is listed in [COMPATIBILITY.md](COMPATIBILITY.md).

**Not supported:** DualShock 3, Wii Remote and Wii U Pro, a Joy-Con on its own, Xbox One pads
without Bluetooth (models 1537 and 1697), and pads that only work with their own 2.4 GHz USB
receiver. Rumble and lights don't work yet on Switch pads or on GameSir pads in HID mode.

**DualShock 4 over USB:** the PS5 already drives a wired DS4 itself, so Master Control leaves it
alone unless you create the empty file `/data/mastercontrol/ds4_usb`.

## What you need

- A PS5 on **firmware 13.60**, jailbroken, with an **ELF loader on port 9021** (elfldr, or the loader your jailbreak leaves).
- **kstuff** (kstuff-lite works), loaded after every restart.
- A way to send a file to port 9021: a payload manager on the console, or a PC on the same network with `nc` or `socat`.
- A browser to open the hub: Omni (from Omnibloom), or any browser on your network.

Master Control has only been tested on firmware 13.60. The code has no firmware-specific
offsets of its own, so other firmware with an ELF loader may work, but I can't test it.

## Install

### With Omnibloom Setup

If you use [Omnibloom](https://github.com/egomortal/omnibloom), its Setup installs Master Control
for you, puts it in autoload and adds the **Master Control** tile. Nothing else to do.

### By hand

1. Download `master-control.elf` from the [Releases page](https://github.com/egomortal/master-control/releases).
2. Send it to the console's ELF loader on port 9021:

   ```sh
   nc -q0 <ps5-ip> 9021 < master-control.elf
   # or
   socat -u FILE:master-control.elf TCP:<ps5-ip>:9021
   ```

   Or use your payload manager's send button.
3. A notification says **Master Control is ready** and shows the hub's address,
   `http://<ps5-ip>:PORT/`. A **Master Control** tile also appears on the Home screen.
4. **Start it on every boot (optional):** add `master-control.elf` to your payload manager's autoload
   list. With Payload Manager that's one line in `/data/pldmgr/autoload.txt`:

   ```
   master-control.elf
   ```

   Don't autoload PadBridge, EeveeLution or ps5-wireless-pads as well: they use the same Bluetooth
   chip, and Master Control would stop them anyway.

> **PORT** is the hub's network port: **8766** by default. Installed with **Omnibloom Setup**, Master
> Control reads its port from its Bloom Core card (`/data/hub/apps/master-control.json`), and Setup picks
> another free one if 8766 is taken; the notification always shows the real address. Master Control no
> longer uses 8095 (OmniPad-PS5's dashboard port) unless you put `8095` in `/data/mastercontrol/port`,
> a manual override.

The jailbreak doesn't survive a full restart: run it again and Master Control comes back from autoload
(or send the ELF again). Rest mode doesn't need that.

### Coming from PadBridge

The first time it starts, Master Control copies your pairings, maps, remaps and settings from
`/data/padbridge/` into its own folder, `/data/mastercontrol/`. PadBridge's folder is never changed,
so you can go back to it by sending the old ELF again.

## Using the hub

Open the **Master Control** tile, choose **Controllers** in Omni, or go to `http://<ps5-ip>:PORT/`
in any browser.

The hub is a dial with five sections:

| Section | What's there |
| --- | --- |
| **Pads** | Every connected controller: its player slot, Bluetooth or USB, battery. **Remap**, **Press PS** (sends a PS-button press for that controller) and **More** (details, Identify to rumble it, Forget) |
| **Pair** | Search for a new Bluetooth controller |
| **Remap** | Change what each button does, per controller model. Paddles and extra buttons are under **Show extra buttons** |
| **Overlay** | The quick overlay inside Omni |
| **Setup** | Notification language, Bluetooth status, the log, About, and Stop |

**With a controller:** L1 / R1 turn the dial, the D-pad moves, Cross selects, Circle goes back,
Square jumps to Pair and Triangle to Remap. A keyboard works too (arrows, Enter, Escape,
Page Up / Page Down).

The old menu is still at `/classic`.

## Pairing a controller

### Easiest: plug in, then unplug (Xbox controllers)

For **Xbox Series X|S** and other Microsoft Xbox controllers you don't need to open any menu or use a phone:

1. **Plug the controller into the PS5** with a USB cable. It works straight away as a wired pad.
2. **Unplug it.** Master Control opens a 60-second Bluetooth search by itself and shows a notification.
3. **Hold the small pair button** on top of the controller until the Xbox button flashes fast.
4. When the "controller paired" notification appears, **turn the controller off and on once**. From then on it connects by itself when you switch it on, with no cable.

The pair button still has to be held once, because no controller accepts a Bluetooth pairing over its cable. You only do this the first time.

It also works without a cable: if nothing is paired yet, Master Control opens the same 60-second search when it starts, so you can just hold the pair button.

Wired-only pads (GameSir, Razer, generic USB) can't go wireless, so unplugging them doesn't start a search. To turn the automatic search off, create the file `/data/mastercontrol/no_auto_pair`.

### From the menu (any Bluetooth controller)

1. In the hub, open **Pair** and press **Search**. Master Control listens for 60 seconds.
2. Put the controller in pairing mode. The Pair screen shows how for each brand. For example:
   - **Xbox:** hold the small pair button on top until the Xbox button flashes fast.
   - **Switch Pro:** hold the sync button on top.
   - **DualShock 4:** hold Share and PS until the light bar flashes.
3. When it's connected it shows up in **Pads**. Press the PS button on it, or **Press PS** in the hub, to pick a user.

After that, just turn the controller on and it reconnects. To pair it to something else, use
**More > Forget** first.

USB pads need no pairing: plug them in.

## Troubleshooting

| Problem | Try this |
| --- | --- |
| No "ready" notification after sending the ELF | Check the loader is up (port 9021 answers) and kstuff is loaded. Look at `/data/hub/logs/master-control/master-control.log` over FTP (`/data/mastercontrol/mastercontrol.log` before 0.4.3). |
| The hub page doesn't open | Use the address from the notification. If it says Bluetooth failed, restart the console, run the jailbreak, and send the ELF again. |
| A controller won't pair | Press **Search** again and put the pad in pairing mode within the 60 seconds. Forget it on any other device it was paired to. Bluetooth LE pads (newer Xbox firmware) need the PS5's chip to do Secure Connections, which isn't confirmed for every model. |
| A USB pad isn't seen | Plug it straight into the PS5, not a hub. Some pads have a mode switch: try XInput / X-mode, or Switch mode for Switch-style pads. |
| It works in menus but not as a second player | Each controller needs its own signed-in user. Sign in a second user, then press PS on that controller and choose them. |
| Buttons are in the wrong place | Use **Remap** for that model, or add a `.map` file (see [maps/EXAMPLE.map](maps/EXAMPLE.map)). |
| "Already running" or two copies | Sending the ELF again replaces the running copy. To stop it, use **Setup > Stop**, or create the file `/data/mastercontrol/stop`. |
| Something else | [Open an issue](https://github.com/egomortal/master-control/issues) with the controller model, your firmware, what happened, and `/data/mastercontrol/mastercontrol.log`. |

## Files on the console

Everything lives in `/data/mastercontrol/`: pairings, maps, remaps, settings, the log
`mastercontrol.log` and the lock `mastercontrol.lock`. Deleting the folder resets Master Control.
Remove the line from autoload to stop it starting.

## Building

See [docs/BUILD.md](docs/BUILD.md). Release ELFs are built by GitHub Actions from this source
([.github/workflows/release.yml](.github/workflows/release.yml)).

## Licence and credits

Master Control is free software under the **GNU General Public License v3.0 or later**
([LICENSE](LICENSE)). It's built from AnyPad-PS5, PadBridge PS5, ps5-wireless-pads and Ghost-Control,
whose authors keep their credit: see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and
[CREDITS.md](CREDITS.md).
