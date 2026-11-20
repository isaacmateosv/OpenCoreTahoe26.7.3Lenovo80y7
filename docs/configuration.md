# Configuration reference

The tables below are generated from `EFI/OC/config.plist` by `tools/gen-config-tables.py`. Regenerate them after editing the config:

```bash
python3 -I tools/gen-config-tables.py
```

## Boot arguments

| Boot-arg | Purpose |
|---|---|
| `shda-lang=en` | SimpleHDA's language option |
| `-nvmefaspm` | NVMeFix: force ASPM L1 on the NVMe SSD, for power saving |
| `unfairgva=4` | WhateverGreen: lets AppleGVA read a separate `hwgva-id`, which MusicGVA answers per process (Apple Music fix, see [Apple Music](apple-music.md)) |

## Security posture

- **SIP is fully on** (`csr-active-config = 0x00000000`), with authenticated root and a sealed system volume. Nothing patches the system volume, so macOS updates install normally. `AMFIPass.kext` is disabled and the old `-amfipassbeta` / `ipc_control_port_options=0` boot-args are gone; none of them were needed (Wi-Fi uses AirportItlwm on the native IO80211 stack).
- **OpenCore:** `SecureBootModel = Disabled`, `Vault = Optional`, `ScanPolicy = 0`.
- **NVRAM:** `boot-args` and `csr-active-config` are deleted and rewritten on every boot (`NVRAM > Delete`), so the config always wins and an NVRAM reset is safe.

## Windows dual boot

Patches that would otherwise reach Windows are limited to macOS:
- The legacy TIMR / RTC / IPIC IRQ binary patches are disabled. DSDT renames (`_CRS` → `XCRS`) plus `SSDT-IRQFIX.aml` give macOS the same IRQ-less resources, and every other OS the originals.
- `SSDT-YOGAPM.aml` (touchscreen `_PS3` delay, honest lid state on wake) only acts under `_OSI("Darwin")`.
- `AudioDxe.efi` runs with `--restore-nosnoop`, so the boot chime does not leave the audio controller in a state Windows can't use.

## Generated tables

<!-- BEGIN GENERATED TABLES -->
### Kexts (load order)

| # | Kext | Version | Enabled |
|---|---|---|---|
| 0 | `Lilu.kext` | 1.7.3 | yes |
| 1 | `CPUFriend.kext` | 1.3.0 | yes |
| 2 | `CPUFriendDataProvider.kext` | 1.0.1 | yes |
| 3 | `AMFIPass.kext` | 1.4.1 | no |
| 4 | `AirportItlwm.kext` | 2.4.0 | yes |
| 5 | `AppleALC.kext` | 1.9.9 | yes |
| 6 | `BlueToolFixup.kext` | 2.7.3 | yes |
| 7 | `BrightnessKeys.kext` | 1.0.4 | yes |
| 8 | `ECEnabler.kext` | 1.0.6 | yes |
| 9 | `IntelBTPatcher.kext` | 2.5.0 | yes |
| 10 | `IntelBluetoothFirmware.kext` | 2.5.0 | yes |
| 11 | `NVMeFix.kext` | 1.1.4 | yes |
| 12 | `RestrictEvents.kext` | 1.1.7 | yes |
| 13 | `VirtualSMC.kext` | 1.3.9 | yes |
| 14 | `SMCBatteryManager.kext` | 1.3.9 | yes |
| 15 | `SMCLightSensor.kext` | 1.3.9 | yes |
| 16 | `SMCProcessor.kext` | 1.3.9 | yes |
| 17 | `SMCSuperIO.kext` | 1.3.9 | yes |
| 18 | `VoodooI2C.kext/Contents/PlugIns/VoodooI2CServices.kext` | 1.0 | yes |
| 19 | `VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext` | 1.1 | yes |
| 20 | `VoodooI2C.kext` | 2.9.1 | yes |
| 21 | `VoodooI2C.kext/Contents/PlugIns/VoodooInput.kext` | 1.1.7 | yes |
| 22 | `VoodooPS2Controller.kext` | 2.3.8 | yes |
| 23 | `VoodooPS2Controller.kext/Contents/PlugIns/VoodooInput.kext` | 1.1.7 | no |
| 24 | `VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Keyboard.kext` | 2.3.8 | yes |
| 25 | `VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Mouse.kext` | 2.3.8 | no |
| 26 | `VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext` | 2.3.8 | no |
| 27 | `VoodooRMI.kext` | 1.4.4 | yes |
| 28 | `VoodooRMI.kext/Contents/PlugIns/RMII2C.kext` | 1.0-yoga2 | yes |
| 29 | `VoodooRMI.kext/Contents/PlugIns/VoodooInput.kext` | 1.1.7 | no |
| 30 | `WhateverGreen.kext` | 1.7.2 | yes |
| 31 | `YogaSMC.kext` | 1.5.3 | yes |
| 32 | `USBMap.kext` | 1.0 | yes |
| 33 | `VoodooI2CHID.kext` | 1.0 | yes |
| 34 | `VoodooRMI.kext/Contents/PlugIns/RMISMBus.kext` | 1.0 | no |
| 35 | `SimpleHDA.kext` | 1.0.1 | yes |
| 36 | `LidWatch.kext` | 1.0.0 | yes |
| 37 | `MusicGVA.kext` | 1.0.0 | yes |

### ACPI tables (EFI/OC/ACPI)

| SSDT | Enabled | Comment |
|---|---|---|
| `SSDT-ALS0.aml` | yes | SSDT-ALS0.aml |
| `SSDT-SBUS.aml` | yes | SSDT-SBUS.aml |
| `SSDT-EC.aml` | yes | SSDT-EC.aml |
| `SSDT-GPI0.aml` | yes | SSDT-GPI0.aml |
| `SSDT-MCHC.aml` | yes | SSDT-MCHC.aml |
| `SSDT-PLUG.aml` | yes | SSDT-PLUG.aml |
| `SSDT-PNLF.aml` | yes | SSDT-PNLF.aml |
| `SSDT-USBX.aml` | yes | SSDT-USBX.aml |
| `SSDT-XOSI.aml` | yes | SSDT-XOSI.aml |
| `SSDT-I2C-SPOOF.aml` | no | DISABLED 2026-11-14: junk TPDX/TPLX clones; TPDX _PS0/_PS3 re-ran the real touchscreen's power methods (double power-off, failed sleep) |
| `SSDT-GPRW.aml` | yes | SSDT-GPRW.aml |
| `SSDT-HPET.aml` | yes | SSDT-HPET.aml |
| `SSDT-ECRW.aml` | yes | SSDT-ECRW.aml |
| `SSDT-LIDW.aml` | yes | SSDT-LIDW.aml - lid check/replay methods for LidWatch.kext |
| `SSDT-YOGAPM.aml` | yes | SSDT-YOGAPM.aml - delayed TPD0 _PS3 (needs XPS3 rename) + LWOP lid check for patched _WAK |
| `SSDT-IRQFIX.aml` | yes | Legacy IRQ fix (timer/RTC/PIC), macOS only |

### ACPI patches

| Comment | Enabled |
|---|---|
| GPI0 _STA to XSTA Rename | yes |
| H_EC _STA to XSTA Rename | yes |
| PNLF to XNLF Rename | yes |
| _OSI to XOSI rename - requires SSDT-XOSI.aml | yes |
| Disable Original TPL1 (Rename _STA to XSTA) | yes |
| Disable Original TPD0 (Rename _STA to XSTA) | yes |
| Fix I2C APIC Interrupt ExclusiveAndWake to Exclusive | yes |
| Fix I2C GpioInt ExclusiveAndWake to Exclusive | yes |
| Rename GPRW to XPRW | yes |
| Change Method(GPRW,2,N) to XPRW | yes |
| HPET _CRS to XCRS Rename | yes |
| HPET _STA to XSTA Rename | yes |
| TIMR IRQ 0 Patch (replaced by SSDT-IRQFIX, macOS-only) | no |
| RTC IRQ 8 Patch (replaced by SSDT-IRQFIX, macOS-only) | no |
| IPIC IRQ 2 Patch (replaced by SSDT-IRQFIX, macOS-only) | no |
| TPD0 touchscreen: rename _PS3 to XPS3 (SSDT-YOGAPM supplies a 2 s delayed _PS3 that calls XPS3) | yes |
| _WAK: report lid open only when it is open (If IGDS -> If LWOP, needs SSDT-YOGAPM) | yes |
| TIMR _CRS to XCRS (needs SSDT-IRQFIX) | yes |
| RTC _CRS to XCRS (needs SSDT-IRQFIX) | yes |
| IPIC _CRS to XCRS (needs SSDT-IRQFIX) | yes |

### UEFI drivers

| Driver | Enabled |
|---|---|
| `AudioDxe.efi` | yes |
| `apfs_aligned.efi` | yes |
| `HfsPlus.efi` | yes |
| `OpenCanopy.efi` | yes |
| `OpenRuntime.efi` | yes |
| `ResetNvramEntry.efi` | yes |

### DeviceProperties

- `PciRoot(0x0)/Pci(0x1f,0x3)`: `alc-delay`=1000, `layout-id`=15000000
- `PciRoot(0x0)/Pci(0x2,0x0)`: `AAPL,ig-platform-id`=00001659, `device-id`=16590000, `enable-max-pixel-clock-override`=01000000, `framebuffer-patch-enable`=01000000, `framebuffer-unifiedmem`=00000080

### NVRAM / security

- boot-args: `shda-lang=en -nvmefaspm unfairgva=4`
- csr-active-config: `00000000`
- SecureBootModel: `Disabled`, Vault: `Optional`, ScanPolicy: `0`
- LauncherOption: `Full`, PickerMode: `External`, ShowPicker: `True`, Timeout: `5`
- BlessOverride: `['\\EFI\\Microsoft\\Boot\\bootmgfw-orig.efi']`
- SMBIOS model: `MacBookPro15,2`. The serial, MLB, UUID and ROM in this repo are placeholders: generate your own (see [installation.md](installation.md#2-generate-your-own-smbios)).
- Kernel quirks enabled: `AppleXcpmCfgLock`, `CustomSMBIOSGuid`, `DisableIoMapper`, `DisableLinkeditJettison`, `PanicNoKextDump`, `PowerTimeoutKernelPanic`
<!-- END GENERATED TABLES -->
