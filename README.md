<div align="center">

# OpenCore EFI for the Lenovo Yoga 920 (80Y7)

**macOS Tahoe 26.7.3 on the Lenovo Yoga 920-13IKB, as a daily driver**

![macOS](https://img.shields.io/badge/macOS-Tahoe_26.7.3_%2825G342%29-000000?logo=apple&logoColor=white)
![OpenCore](https://img.shields.io/badge/OpenCore-1.0.9-1f6feb)
![SMBIOS](https://img.shields.io/badge/SMBIOS-MacBookPro15%2C2-555555)
![License](https://img.shields.io/badge/license-Unlicense-2ea44f)

</div>

This is a complete, tested OpenCore EFI for the Yoga 920 convertible. It also includes fixes for the problems that make this machine hard to live with on macOS: trackpad lag after sleep, 180-second sleep panics caused by the touchscreen, lid closes that never put the Mac to sleep, auto-rotate, and an Apple Music that plays no sound. Every fix was traced to its root cause on this hardware and verified there. The write-ups explain why each one works.

> [!IMPORTANT]
> The config ships with **placeholder SMBIOS serials**. Generate your own before you sign in to iCloud, iMessage or the App Store: see [Installation › Generate your own SMBIOS](docs/installation.md#2-generate-your-own-smbios).

## Highlights

- **Smooth trackpad after every wake.** The firmware resets the trackpad's interrupt polarity on resume, which floods the driver with ~60,000 interrupts/s. A patched VoodooRMI/RMII2C re-arms the pad and handles the line as level-triggered: one interrupt per report. → [Trackpad](docs/trackpad.md)
- **No more 180 s sleep panics.** A delayed touchscreen power-off removes a race between the ACPI `_PS3` method and VoodooI2CHID's sleep command. → [Touchscreen sleep](docs/touchscreen-sleep.md)
- **Reliable lid sleep.** The LidWatch kext replays the lid events the embedded controller occasionally loses, and a `_WAK` patch keeps lid-shut timer wakes dark. → [Lid](docs/lid.md)
- **Auto-rotate with a single switch.** A low-cost accelerometer reader (0 interrupts/s) is toggled by one Shortcut, and the touchscreen follows the rotation. → [Auto-rotate](docs/auto-rotate.md)
- **Apple Music works, Dolby Atmos included.** A small login agent works around a FairPlay failure on Intel graphics in macOS 26. → [Apple Music](docs/apple-music.md)
- **Full turbo.** CPUFriend with EPP 0x40 lifts single-core clocks from 2.87 GHz to 3.72 GHz. → [Performance](docs/cpu-performance.md)

## Hardware

| Part | Details |
|---|---|
| Model | Lenovo Yoga 920-13IKB, type **80Y7** |
| CPU | Intel Core i7-8550U (Kaby Lake-R), native XCPM power management |
| iGPU | Intel UHD 620, spoofed as HD 620 (`device-id 0x5916`, `ig-platform-id 0x59160000`), Metal 3, 2048 MB |
| Display | 13.9" 4K (3840×2160) eDP touchscreen, HiDPI |
| Storage (as tested) | Samsung 980 1 TB NVMe |
| Wi-Fi / Bluetooth | Intel Wireless 8265: AirportItlwm, IntelBluetoothFirmware |
| Audio | Realtek codec: AppleALC layout 21 + SimpleHDA (macOS 26 no longer ships AppleHDA) |
| Trackpad | Synaptics SYNA2B31 (TM3260) on I2C1 (`TPL1`): VoodooRMI + patched RMII2C |
| Touchscreen / pen | Wacom WCOM5110 on I2C0 (`TPD0`): VoodooI2C + VoodooI2CHID |
| Sensor hub | ITE8353 on I2C3 (`SHUB`, accelerometer + light sensor): drives auto-rotate |
| Firmware | OpenCore 1.0.9 (REL-109-2026-11-08), SMBIOS MacBookPro15,2 |

## Status

| Feature | Status | Notes |
|---|:---:|---|
| Graphics: Metal 3, 4K HiDPI | ✅ | Use **1920×1080 (Default)**, the native 2× mode; scaled modes stutter |
| Hardware video decoding (HEVC, H.264) | ✅ | VideoToolbox through Apple's Intel driver |
| CPU power management, full turbo | ✅ | XCPM + CPUFriend (EPP 0x40) |
| Battery, AC adapter | ✅ | VirtualSMC + SMCBatteryManager; Lenovo EC via YogaSMC |
| Wi-Fi | ✅ | Native Wi-Fi menu (AirportItlwm). WPA3-only networks don't connect: use WPA2/WPA3 mixed mode |
| Bluetooth | ✅ | IntelBluetoothFirmware + IntelBTPatcher + BlueToolFixup |
| Speakers, headphone jack, USB-C audio, boot chime | ✅ | |
| Keyboard, brightness keys | ✅ | VoodooPS2 + BrightnessKeys |
| Trackpad with gestures, also after sleep | ✅ | Patched VoodooRMI 1.4.4 + RMII2C |
| Touchscreen | ✅ | Follows screen rotation |
| Auto-rotate | ✅ | One Shortcut switch (`tools/autorotate`) |
| Lid sleep and wake | ✅ | Wake in ~1.2–1.4 s |
| Booting from the internal disk, no USB | ✅ | Needs the Lenovo boot-path step in the [installation guide](docs/installation.md#4-install-to-the-internal-disk) |
| Apple Music streaming, Dolby Atmos | ✅ | With `tools/musicfix` installed |
| Hibernation | ⚠️ | Off by design (OpenCore `HibernateMode = None`); sleep is S3 only |
| Tent / tablet mode | ⚠️ | Not tested yet together with LidWatch |
| AirDrop, Universal Clipboard, Continuity | ❌ | The Intel card has no AWDL; needs a different Wi-Fi card |
| DRM-protected video: Apple TV app shows and movies, music videos in Music, Safari | ❌ | FairPlay video needs the Intel GPU's hardware DRM path, which fails here, and macOS has no software route for it. Trailers and previews play. Workaround: `tv.apple.com` / `music.apple.com` in a Chromium-based browser (Widevine) ([details](docs/apple-music.md#what-still-doesnt-play-protected-video)) |
| Fingerprint reader | ❌ | macOS supports only Apple's Touch ID |

## Quick start

1. **Read [docs/installation.md](docs/installation.md).** In the BIOS, disable **Secure Boot**.
2. **Generate your own SMBIOS** (MacBookPro15,2) and put it in `PlatformInfo > Generic`.
3. **Test from a USB stick** before you touch the internal disk.
4. **Install to the internal EFI** and apply the Lenovo boot-path step.
5. **After the first boot**, apply the sleep profile and run the tools installer: [docs/post-install.md](docs/post-install.md).

```bash
tools/install-tools.sh   # Apple Music fix + Auto-Rotate switch; no sudo, safe to run again
```

## Documentation

| Guide | What it covers |
|---|---|
| [Installation](docs/installation.md) | BIOS, SMBIOS, USB test, internal install, Lenovo boot path |
| [Post-install](docs/post-install.md) | Sleep profile, tools installer, display, Wi-Fi, accounts |
| [Trackpad](docs/trackpad.md) | The post-wake interrupt storm and the patched VoodooRMI/RMII2C |
| [Touchscreen sleep](docs/touchscreen-sleep.md) | The 180 s sleep panic, the wake freeze, and their fix |
| [Lid](docs/lid.md) | LidWatch and the honest lid state on wake |
| [Auto-rotate](docs/auto-rotate.md) | Sensor hub, polling reader, the Shortcut switch |
| [Apple Music](docs/apple-music.md) | The FairPlay failure, `musicfix`, Dolby Atmos, Apple TV limits |
| [Performance](docs/cpu-performance.md) | CPU turbo (CPUFriend) and GPU settings |
| [Configuration](docs/configuration.md) | Every kext, SSDT, ACPI patch and setting in the config |
| [Maintenance](docs/maintenance.md) | Updates, risks, recovery, optional cleanup |
| [Troubleshooting](docs/troubleshooting.md) | Diagnostic tools and what healthy output looks like |
| [Changelog](CHANGELOG.md) | How this EFI got here |

## Repository layout

```text
EFI/                      OpenCore EFI (BOOT + OC), placeholder SMBIOS
acpi-dump/                the 80Y7's original ACPI tables, for patch development
                          (MSDM removed: it holds the Windows product key)
patches/                  sources of every custom kext and SSDT, with build scripts
  LidWatch/               lid-event watchdog kext + SSDT-LIDW
  RMII2C-wakefix2/        patched VoodooRMI I2C transport (GPL-2.0)
  VoodooRMI-1.4.4-yoga1/  patched VoodooRMI (GPL-2.0)
  SSDT-YOGAPM/            delayed touchscreen _PS3 + honest lid state on wake
  CPUFriend-EPP40/        generator for CPUFriendDataProvider.kext
tools/                    installer, musicfix, autorotate, rotate, diagnostics
docs/                     the guides listed above
```

## Credits

This EFI stands on the work of [Acidanthera](https://github.com/acidanthera), the [VoodooI2C](https://github.com/VoodooI2C) team, [1Revenger1](https://github.com/1Revenger1), [OpenIntelWireless](https://github.com/OpenIntelWireless), [zhen-zen](https://github.com/zhen-zen), [laobamac](https://github.com/laobamac), [Dortania](https://github.com/dortania) and [CorpNewt](https://github.com/corpnewt). The Apple Music fix builds on findings shared in [JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe#11](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe/issues/11) and [b00t0x/WhateverXe#48](https://github.com/b00t0x/WhateverXe/issues/48). The full list, with links, is in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Feedback

Issues and pull requests are welcome, especially from owners of the same machine with a different BIOS version. Please include your macOS build, BIOS version and the output of the relevant check from [Troubleshooting](docs/troubleshooting.md).

## License

Original work in this repository (docs, tools, SSDTs, LidWatch, build scripts) is released under [The Unlicense](LICENSE). The modified VoodooRMI sources in `patches/` remain under VoodooRMI's GPL-2.0. Bundled third-party binaries keep their upstream licenses; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Disclaimer

Not affiliated with Apple, Lenovo or any upstream project. Use at your own risk: back up your data and your EFI partition first. macOS is licensed by Apple for Apple-branded hardware; know the rules where you live.
