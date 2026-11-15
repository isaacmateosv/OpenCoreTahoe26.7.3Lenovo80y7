# Installation

Tested on a Lenovo Yoga 920-13IKB (80Y7) with macOS Tahoe 26.7.3 (25G342) and OpenCore 1.0.9. A different BIOS version can change the ACPI tables that some patches match; see [Maintenance › BIOS updates](maintenance.md#bios-updates).

This guide covers what is specific to this machine. To create the macOS installer USB itself, follow [Dortania's OpenCore Install Guide](https://dortania.github.io/OpenCore-Install-Guide/).

## 1. Before you start

- Back up your data and your current EFI partition.
- **BIOS: disable Secure Boot.** OpenCore here is not signed for UEFI Secure Boot (`SecureBootModel` is `Disabled`).
- **CFG Lock** does not need to be unlocked: the config enables the `AppleXcpmCfgLock` quirk.
- Useful tools: [ProperTree](https://github.com/corpnewt/ProperTree) (plist editor) and [GenSMBIOS](https://github.com/corpnewt/GenSMBIOS), or `macserial` from OpenCore's `Utilities` folder.

## 2. Generate your own SMBIOS

`EFI/OC/config.plist` ships with OpenCore's sample placeholders. Replace the four identity fields in `PlatformInfo > Generic`:

| Key | Placeholder | What to put there |
|---|---|---|
| `SystemProductName` | `MacBookPro15,2` | Keep it |
| `SystemSerialNumber` | `W00000000001` | From GenSMBIOS ("Generate SMBIOS", model `MacBookPro15,2`) or `macserial -m MacBookPro15,2` |
| `MLB` | `M0000000000000001` | The board serial printed next to that serial |
| `SystemUUID` | `00000000-0000-0000-0000-000000000000` | Output of `uuidgen` |
| `ROM` | `112233445566` | Your Wi-Fi MAC address without colons, as 6 bytes of data |

Then check the serial at [checkcoverage.apple.com](https://checkcoverage.apple.com). It should be reported as **not valid**, which means no real Mac uses it. If it belongs to a real Mac, generate another one.

Apple limits how many new Apple Accounts can be **created** from one Mac serial. Create new accounts on the web or on an iPhone, then sign in here.

## 3. Test from a USB stick

1. Format a USB stick as FAT32 and copy this repo's `EFI` folder to its root.
2. Boot it from the firmware's boot menu.
3. Install or boot macOS as usual. Nothing on the internal disk changes until step 4.

## 4. Install to the internal disk

1. Mount the internal EFI partition. It is not mounted automatically:
   ```bash
   diskutil list                  # find the EFI partition of the internal disk
   sudo diskutil mount disk0s1    # adjust the identifier if yours differs
   ```
2. Keep a copy of what is there, then copy this repo's `EFI/BOOT` and `EFI/OC` into `/Volumes/EFI/EFI/` (replace `OC` entirely).
3. **Lenovo boot-path step.** On this machine the firmware only boots the Windows loader path from the internal disk, so OpenCore takes that path and chains to Windows:
   - Once: rename `/Volumes/EFI/EFI/Microsoft/Boot/bootmgfw.efi` to `bootmgfw-orig.efi`. This is the real Windows loader.
   - Copy `EFI/BOOT/BOOTx64.efi` to `/Volumes/EFI/EFI/Microsoft/Boot/bootmgfw.efi`.
   - The config already has `BlessOverride` set to `\EFI\Microsoft\Boot\bootmgfw-orig.efi`, so Windows stays in the OpenCore picker, and `LauncherOption` is `Full`.
   - A Windows update can overwrite `bootmgfw.efi`; the symptom is booting straight into Windows. Just redo the copy.

   This was tested with Windows installed alongside macOS. Without Windows, create `EFI/Microsoft/Boot/` and put the copy there yourself (untested).
4. Reboot.

> [!TIP]
> Keep the USB stick from step 3. It is your rescue disk if an update or an experiment ever leaves the internal EFI unbootable.

## 5. After the first boot

Continue with [Post-install](post-install.md): the sleep profile and `tools/install-tools.sh` (Apple Music fix and the Auto-Rotate switch).
