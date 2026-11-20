# MusicGVA — Apple Music *and* hardware video decoding, with full SIP

**Status:** included in this EFI (`EFI/OC/Kexts/MusicGVA.kext`, `Kernel > Add`, boot-arg `unfairgva=4`). Verified 2026-11-20 on macOS 26.7.3 with full SIP: hardware decode H.264/HEVC 60/60 frames, Music plays, no panics.

## Problem
On this Yoga 920 (UHD 620, Apple's KBL driver, macOS 26.7.3) Music's FairPlay first tries the Intel GPU hardware key
exchange; it fails here (`err = -15842`) and Music never falls back, so songs sit at 0:00.

* `musicfix` (sandbox Music away from the Intel VA driver) worked while SIP was partly off. With **full SIP**, launch
  constraints kill any Music not started by LaunchServices (`CODESIGNING 4, Launch Constraint Violation`), even with
  launchd as the parent. Uninstalled.
* WhateverGreen `unfairgva=4` makes Music work with full SIP: it patches AppleGVA in the shared cache to read the
  device-tree root property `hwgva-id` instead of `board-id`, and sets `hwgva-id` = iMacPro1,1. But the patch is
  global: **no app gets hardware video decoding** (VideoToolbox "require hardware" fails with -12913).

## Fix
`MusicGVA.kext`, a ~30 KB Lilu plugin (MIT), keeps WhateverGreen's AppleGVA patch and answers reads of `hwgva-id` on
the device-tree root **per process**:

| caller | sees `hwgva-id` | result |
|---|---|---|
| `Music` | `Mac-7BA5B2D9E42DDD94` (iMacPro1,1) | software FairPlay → plays (as with unfairgva=4) |
| everything else | the real `board-id` (`Mac-827FB448E656EC26`) | stock AppleGVA → hardware decode |

Two kernel functions are routed, both acting only when the object is the device-tree root:
`IORegistryEntry::copyProperty(const char *)` (all single-key reads from user space) and
`IORegistryEntry::serializeProperties` (whole-table reads, e.g. `IORegistryEntryCreateCFProperties`, `ioreg -l`).
No user-space binary or system file is modified; SIP stays fully on.

Kill switch: boot-arg `-musicgvaoff`. Debug build (`prebuilt/debug`, logs with `-musicgvadbg`).

## Files
* `src/` + `build.sh` — source (Lilu 1.7.3 headers from github.com/acidanthera/Lilu master @0249c3a, MacKernelSDK
  @3f75008), builds with Xcode only.
* `prebuilt/MusicGVA.kext` — RELEASE build. All 305 imports resolve (kmutil libraries: Lilu + kpi.bsd/iokit/libkern).
* `verify.sh` — after reboot: kext loaded, kernel log, `hwgva-id` as seen by ioreg (must be the real board-id),
  hardware decode test (H.264 + HEVC, hardware required), Music check.

## If it goes wrong
* No boot / panic: boot a USB stick with an EFI without MusicGVA, or add `-musicgvaoff` to the boot-args.
* Boots but Music silent: the AppleGVA read path differs from what the hooks cover → disable the `MusicGVA.kext` entry (back to plain unfairgva=4: Music works, software video).
* Hardware decode still off: open an issue with the `./verify.sh` output.
