# Performance: CPU turbo and GPU settings

## CPU: CPUFriend with EPP 0x40

**Symptom:** macOS's CPU power manager (XCPM) allows 4.0 GHz, yet the CPU runs at a flat 2.87 GHz single-core.

**Cause:** the MacBookPro15,2 power profile (`Mac-827FB448E656EC26.plist`) sets the Speed Shift energy hint to **EPP = 0x90** in all three FrequencyVectors. That value is tuned for a 28 W chip, and it holds back a 15 W i7-8550U.

**Fix:** [CPUFriend](https://github.com/acidanthera/CPUFriend) 1.3.0 plus `CPUFriendDataProvider.kext`. The data provider is Apple's own profile with only the EPP byte changed, to **0x40** ("balanced performance"). Rebuild it, or choose another EPP value, with:

```bash
python3 -I patches/CPUFriend-EPP40/make-dataprovider.py 0x40 <output dir>
```

The script produces the same kext layout as Acidanthera's `ResourceConverter.sh`.

| | EPP 0x90 (stock) | EPP 0x40 |
|---|---|---|
| Single core | 2.87 GHz | **3.72 GHz** (3.7 GHz is Intel's turbo bin with 3–4 cores active) |
| All cores, burst | ~2.8 GHz | **~3.15 GHz** |
| All cores, sustained | ~2.2 GHz | ~2.3 GHz (BIOS power limit PL1 = 15 W) |

Measured with `tools/freqtest.c`, a dependent-add clock meter that needs no sudo:

```bash
clang -O2 -pthread -o tools/freqtest tools/freqtest.c && tools/freqtest
```

For maximum battery life on the go, macOS **Low Power Mode** caps the CPU at 2.6 GHz (XCPM `lpm_plimit` 26). It is the practical "turbo off" switch, since TurboBoost Switcher's kext doesn't load on Tahoe.

## GPU: UHD 620 settings

- **`framebuffer-unifiedmem` = 2048 MB.** The iGPU reports 2048 MB of VRAM (Metal 3).
- **Display at native 2×.** Use 1920 × 1080 (Default) on the 4K panel. Scaled modes make the iGPU resample every frame, and animations stutter.
- **Mission Control at 4K is GPU-bound.** It peaks at 100% on the UHD 620; that is the hardware limit. Reduce Transparency helps.
- **AGPM never power-limits the GPU** (`PlimitNS` = 0).

**Tried and rejected:**
- **WhateverGreen `rps-control`.** Measured with `tools/gpubusy.c` on the same Mission Control and desktop-swipe routine, GPU load while animating rose from 32.7% to 54.5%, and maxed-out samples (≥80%) from 8% to 23%. The iGPU clocked lower and animations felt glitchier, so it was reverted.
- **`igfxfw=2`.** Apple's GuC firmware needs a newer chipset than this 100-series PCH; don't use it.
