# Maintenance

Nothing in this setup should break on its own. The risks come from outside events: macOS, Windows, BIOS or kext updates. This page lists each one and what to do.

## What can break it, and what to do

| Event | Risk | What to do |
|---|---|---|
| macOS 26.x update | Medium | **Before:** keep a USB stick with your working EFI. **After:** run `tools/trackpad-check.sh` and `tools/lidwatch-check.sh selftest`, do one lid test, and check audio, Wi-Fi and `~/.local/bin/musicfix status`. If anything breaks, boot from the USB stick. There is no macOS 27 for Intel Macs. |
| Updating VoodooRMI, VoodooI2C or VoodooI2CHID | **High if done blindly** | VoodooRMI and RMII2C are custom builds (`patches/`). Upstream versions bring back the post-wake trackpad lag and the F12 panic. If you update VoodooI2C, rebuild RMII2C against it. A new VoodooI2CHID brings back its "Sensor Hub Event Driver" personality: delete it from the new `Info.plist` again, or the stock driver and `autorotate` will both rotate the screen. |
| Windows Update | Low, but annoying | It may overwrite `\EFI\Microsoft\Boot\bootmgfw.efi`; the symptom is booting straight into Windows. Copy `EFI/BOOT/BOOTx64.efi` over it again ([installation](installation.md#4-install-to-the-internal-disk)). |
| <a id="bios-updates"></a>BIOS update | Medium | A new DSDT can stop the ACPI patches from matching, including `_PS3`, `_WAK` and the `_STA` renames. Avoid BIOS updates, or dump the new ACPI tables (OpenCore SysReport) and check every patch still matches exactly once. |
| Windows booted from the OpenCore picker | Low | OpenCore also applies the macOS ACPI patches to Windows. The only visible effect is that the touchscreen powers off 2 s later during Windows sleep. |
| macOS update changes Music's DRM | Low | If songs stay at 0:00 again, run `musicfix status` and read its log. If the GPU driver bundles are renamed, update the two paths in `tools/musicfix.m`. If `sandbox-exec` ever disappears, the agent's loop guard leaves Music alone and posts a notification. |
| Tent / tablet mode | Unknown | Not tested yet with LidWatch. If folding the screen back makes the Mac sleep, please open an issue. |

## Notes on the EFI

- **Kexts.** Only one VoodooInput is active: VoodooI2C's. The copies inside VoodooPS2 and VoodooRMI are disabled. `RMISMBus` is disabled too, because this trackpad is on I2C, not SMBus.
- **ACPI.** On the tested BIOS, every table loads with **0 errors**. The only message is a benign `SHAD._DSM` argument-type warning.
  - **Working patches:** the GPI0, H_EC, TPL1 and TPD0 `_STA` renames, PNLF, `_OSI` (12 sites), GPRW, TPD0 `_PS3`, and `_WAK`.
  - **No-op patches** (0 matches on this BIOS, harmless): both "ExclusiveAndWake" fixes, the duplicate "Change Method(GPRW,2,N)", HPET `_CRS`/`_STA`, and the TIMR/RTC/IPIC IRQ patches.
  - **`SSDT-HPET.aml` is dead weight:** its HPET `_CRS`/`_STA` duplicate the firmware's, because the renames it relies on don't match this BIOS. Audio and timers work without it.
- **Boot path.** `\EFI\Microsoft\Boot\bootmgfw.efi` is a copy of the OpenCore launcher, and the real Windows loader is kept as `bootmgfw-orig.efi` (`BlessOverride`).

## Optional cleanup

You can disable the no-op ACPI patches and `SSDT-HPET.aml`. Nothing changes at runtime; it only removes confusion, and anything that could start matching after a BIOS change.

## Recovery

- **Rescue USB:** keep a FAT32 USB stick with a working copy of `EFI/`. Boot it from the firmware's boot menu.
- **Back up before every config edit,** for example `config.plist.bak-<date>-<what>` next to the original on the EFI partition.
- **Remove LidWatch:** delete the `LidWatch.kext` and `SSDT-LIDW.aml` entries from `config.plist`.
- **Remove the touchscreen and lid fixes:** delete `SSDT-YOGAPM.aml` **and** both of its DSDT patches together.
- **Remove the tools:** run `tools/install-tools.sh --uninstall`. It removes the Music agent, switches Auto-Rotate off and deletes the programs.
