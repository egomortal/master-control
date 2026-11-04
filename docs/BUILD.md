# Building Master Control

You need Linux (or WSL, or macOS for the host tests), `make`, a C compiler, `python3` and `xxd`.
For the console ELF you also need the **PS5 Payload SDK** (<https://github.com/ps5-payload-dev/sdk>)
and clang 18 with lld 18.

## Host tests (no SDK needed)

```sh
make test     # unit tests: Bluetooth host, LE, profiles, web API, remapping, Ghost-Control USB drivers, ...
make fuzz     # malformed input through every parser, under the sanitizers
make demo     # the hub against a simulated controller, to look at in a browser
```

The hub's browser tests are in `tests/ui/` (Playwright with Chromium; see the comments in `ui-test.cjs`).

## The console ELF

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
python3 tools/build_page.py   # only if you changed web/: rebuilds src/web_page.c and src/web_classic.c
make ps5                      # -> dist/master-control.elf
make ps5-icon-installer       # -> dist/PadBridge-PS5-icon-installer.elf (optional tile installer)
```

`src/web_page.c`, `src/web_classic.c` and `src/icon_data.c` are generated (the hub page with its fonts
and art, the old menu, and the tile icon) and are kept in git, so a console build doesn't need Python.

Without installing anything, `tools/build-elf-docker.sh /path/to/ps5-payload-sdk` builds the ELF in an
Ubuntu 24.04 container with clang 18 and lld 18.

## Sending it to the console

```sh
nc -q0 <ps5-ip> 9021 < dist/master-control.elf
```

(`make send` still looks for the old `dist/PadBridge-PS5-*.elf` name; use `nc` until that's fixed.)

## Version

The version is set in one place, `src/version.h`. Raise it with every build that leaves your machine and
add a line to [CHANGELOG.md](../CHANGELOG.md). It shows in the log's first line, the notification, the hub
and the binary (`strings master-control.elf | grep mastercontrol-version`).

## Releases

Publishing a GitHub Release runs [.github/workflows/release.yml](../.github/workflows/release.yml):
host tests, then the ELF with the pinned SDK, then `master-control.elf` and its `.sha256` are attached
to the release. GitHub adds the source archives itself.
