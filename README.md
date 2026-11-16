# kbdlight

Control the keyboard backlight on Clevo-based laptops (tested on **Axioo
Pongo 725**, Clevo NH5xR) from a small keyboard-driven window that follows
the Omarchy theme.

![screenshot](docs/screenshot.png)

## What works

| Feature | Status |
|---|---|
| Brightness 0-9 (off / dim / bright) | **works** |
| Turn off | **works** |
| Colour (RGB) | **red only on this model** — see below |

### About colour

The firmware exposes a colour command (`\_SB_.WMI_.SCMD`, id `0x67`, mode
`0xF`) that forwards three payload bytes to the embedded controller. On the
Pongo 725 those bytes drive a **single red channel**: sending full value on
A, B or C all produce red. The LED is a single-colour (blue/red) backlight,
not an addressable RGB strip, so arbitrary colours are not available.

The **Diagnostics** panel sends each channel alone so you can confirm this on
your own machine. If your model does have RGB, the panel tells you which
channel drives which colour.

## Build and install

```bash
./bin/build     # builds build/kbdlight and build/kbdlight-ctl
./bin/install   # needs sudo: helper is setuid root
```

The app then appears in the launcher as **Keyboard Light**.

## How it talks to the hardware

`kbdlight-ctl` is a small setuid-root helper that writes a single ACPI call:

```
\_SB_.WMI_.SCMD 0 0x67 <ARGS dword>
```

into `/proc/acpi/call`, which requires the `acpi_call` kernel module
(`pacman -S acpi_call-dkms`). The helper accepts only a fixed argument
vocabulary and never evaluates a shell string, so the setuid bit does not
widen the attack surface beyond "set the keyboard light".

The GUI never runs as root; it calls the helper and reports its exit status,
so a failure is shown in the window instead of being swallowed.

### ARGS encoding (from the DSDT)

| Bits | Meaning |
|---|---|
| 28-31 | mode: `0xD` brightness, `0xF` colour |
| 24-27 | zone: 0 = all |
| 16-23 | payload byte A |
| 8-15 | payload byte B |
| 0-7 | payload byte C |
| 12-15 | brightness level 1-9, `>= 0xA` = off |

## Shortcuts

| Key | Action |
|---|---|
| `Space` | Apply brightness and colour |
| `Q` | Quit |
| `?` | Show help |

## Layout

```
kbdlight.pro          qmake project
bin/build             qmake6 + make into build/
bin/install           installs binary, setuid helper, desktop entry, icon
src/main.cpp          app setup
src/backend.{h,cpp}   talks to the helper, owns state
src/theme.{h,cpp}     Omarchy accent + Hyprland rounding
src/kbdlight-ctl.c    setuid helper that writes the ACPI call
src/Main.qml          the window
pkgbuild/             .desktop and icon
```

## Licence

MIT
