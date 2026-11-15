# Trackpad: patched VoodooRMI and RMII2C

The Synaptics SYNA2B31 (TM3260) trackpad sits on I2C1 (`TPL1`), with its interrupt on GPIO pad GPP_B3 (pin 27). It runs on [VoodooRMI](https://github.com/VoodooSMBus/VoodooRMI) through its I2C transport, RMII2C. Both kexts in this EFI are custom builds; their sources and build scripts are in `patches/`. They are built with the Xcode Command Line Tools against the macOS SDK's Kernel.framework, and ABI checks show their vtables match the stock binaries slot for slot.

> [!WARNING]
> Don't replace these two kexts with upstream builds. That brings back the post-wake lag and the F12 boot panic described below. If you update VoodooI2C, rebuild RMII2C against it.

## RMII2C "wake fix 2" (`CFBundleVersion 3`)

**Symptom:** after sleep, the trackpad lags and stutters, and the driver is busy 100% of the time.

The cause was found with the kernel's interrupt counters and snapshots of the pad registers. There were four separate problems:

1. **The firmware resets the pad's polarity bit after S3, and VoodooGPIO never restores it.** `PADCFG0.RXINV` goes from boot `0x80900102` to wake `0x80100100`. The active-low interrupt line then reads as permanently asserted: ~60,000 interrupts/s, the driver reading empty reports back to back, and real touches queued behind them.
   **Fix:** re-register the GPIO interrupt on every wake, so VoodooGPIO programs the pad again.
2. **VoodooGPIO reports the level-triggered pad as edge-triggered**, so the line was never masked: ~100 interrupts per report even when healthy.
   **Fix:** level one-shot handling. The primary handler masks the line, and the read unmasks it. Now there is exactly **1 interrupt per report**.
3. **`setPowerState` used `attemptAction()`**, which silently does nothing while the driver is busy, so the line stayed armed through sleep.
   **Fix:** mask first, then `runAction()`.
4. **Hardening:** I2C-HID `SET_POWER` sleep/on (as Linux does), stuck-line detection with escalating recovery (re-arm → power on → I2C-HID reset → polling fallback), a fix for a recursive lock in `readBlock()`, and live counters in the IORegistry (`RMII2C Stats`).

## VoodooRMI 1.4.4 "Yoga 920 patch 1"

Built from upstream VoodooRMI 1.4.3 (commit 83868f0). `patches/VoodooRMI-1.4.4-yoga1/patch.py` applies the changes, and the result is versioned 1.4.4.

- **F12 register-descriptor overread fix.** Upstream, like Linux, can read past the 32-byte descriptor buffer. macOS's guard allocator turns that into a rare boot panic ("Probabilistic GZAlloc out-of-bounds"). The fix uses a zero-padded buffer and bounded loops.
- **Configurable palm cutoff, `PalmRejectionMaxZ`** (default 200). Upstream hard-codes 140, which turned a firm thumb into a "palm" and broke thumb pinches. It can be tuned live through the RMIBus `Configuration`.
- **Contact diagnostics:** `Contact Stats` on F12 counts touches and palm rejections by reason, and keeps a per-touch max-Z histogram.

## Results

- 1 interrupt per trackpad report.
- After every wake the pad is re-armed, with 0 empty reads and 0 recoveries.
- Thumb contacts peak at Z 77–89, well under the 200 cutoff.
- No F12 panic since the fix.

## Check it

```bash
tools/trackpad-check.sh 5      # loaded versions, RMII2C Stats, interrupt rates (hands off = idle reading)
python3 -I tools/rmistat.py    # per-interval rates from RMII2C Stats
ioreg -rc F12 | grep -A3 "Contact Stats"
```

In a healthy reading the interrupt rate matches the report rate (about 0 when idle), and `Recoveries` stays at 0.

## Rebuild

```bash
patches/RMII2C-wakefix2/build.sh          # -> patches/RMII2C-wakefix2/out/RMII2C.kext
patches/VoodooRMI-1.4.4-yoga1/build.sh    # -> patches/VoodooRMI-1.4.4-yoga1/out/VoodooRMI.kext
```

Put `VoodooRMI.kext` in `EFI/OC/Kexts/`, and `RMII2C.kext` inside it at `Contents/PlugIns/`. Both folders carry VoodooRMI's GPL-2.0 license.
