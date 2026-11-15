# Changelog

How this EFI reached its current state on macOS Tahoe 26.7.3 (25G342). Each entry was verified on the hardware before the next one was made.

## 2026-11-15: first public release

**Touchscreen sleep**
- Fixed the 180 s sleep panic ("Suspected bundle: com.alexandred.VoodooI2CHID"): the touchscreen's `_PS3` power cut raced VoodooI2CHID's sleep command. The first fix renamed `TPD0._PS3` to `XPS3`. The final fix, `SSDT-YOGAPM.aml`, runs the original power cut 2 s later, which also cures the wake freeze the first fix caused. ([details](docs/touchscreen-sleep.md))

**Lid**
- Honest lid state on wake: the DSDT `_WAK` patch `If (IGDS)` → `If (LWOP)`, so lid-shut timer wakes stay dark. ([details](docs/lid.md#honest-lid-state-on-wake))

**Auto-rotate**
- Re-enabled the ITE8353 sensor hub, and removed VoodooI2CHID's "Sensor Hub Event Driver" personality.
- Added `tools/autorotate`: a polling reader with 0 interrupts/s, toggled by one Shortcut. ([details](docs/auto-rotate.md))

**Apple Music**
- Added `tools/musicfix`, which works around the FairPlay `-15842` failure on Intel graphics. Songs and Dolby Atmos play. ([details](docs/apple-music.md))
- Documented that protected video (Apple TV shows, music videos, Safari DRM) can't play on this hardware.

**Config and tools**
- Removed `shikigva=80` from the boot-args; it is inert on macOS 11+.
- Added `tools/install-tools.sh`: one command builds and installs the tools and imports the Shortcut.

## 2026-11-14

- Disabled `SSDT-I2C-SPOOF.aml`. Its fake `TPDX` device powered the touchscreen off twice on every sleep and once hung a sleep.
- Added `LidWatch.kext` and `SSDT-LIDW.aml`, a watchdog that replays lid events the EC loses. ([details](docs/lid.md))
- Tested WhateverGreen `rps-control` and rejected it: it made GPU load while animating higher and animations glitchier.

## 2026-11-13

- First confirmed-working EFI on Tahoe 26.7.3.
- **Trackpad:** VoodooRMI 1.4.4 "Yoga 920 patch 1" (F12 overread boot-panic fix, configurable palm cutoff) and RMII2C "wake fix 2", which ends the post-wake interrupt storm. ([details](docs/trackpad.md))
- **Graphics:** iGPU `framebuffer-unifiedmem` for 2048 MB of VRAM; display at native 2×.
- **CPU:** CPUFriend 1.3.0 with EPP 0x40, for single-core turbo of 2.87 → 3.72 GHz. ([details](docs/cpu-performance.md))
- **Audio:** SimpleHDA + AppleALC layout 21, because Tahoe removed AppleHDA.
- **Sleep:** the profile with no hibernation and a 15-minute battery sleep. ([details](docs/post-install.md#sleep-profile))
