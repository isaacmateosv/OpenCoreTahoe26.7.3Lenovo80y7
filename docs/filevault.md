# FileVault (disk encryption)

Without FileVault, the macOS login password protects nothing once someone holds the laptop. They can boot Recovery, Windows or a Linux USB stick, or pull the SSD and read it in another computer, and then copy your files or reset your password. FileVault encrypts the macOS data volume. Without your password or recovery key it is unreadable, and Recovery can't reset anything.

FileVault works on this machine. Apple's own pre-boot unlock screen runs through OpenCore.

## OpenCore settings (already set in this config)

| Setting | Value | Why |
|---|---|---|
| `UEFI > ProtocolOverrides > FirmwareVolume` | `true` | Lets Apple's unlock screen run on non-Apple firmware |
| `UEFI > ProtocolOverrides > AppleSmcIo` | `true` | The unlock screen talks to the SMC before macOS loads. VirtualSMC.kext only exists once macOS runs, so OpenCore provides the SMC I/O protocol (this replaces `VirtualSmc.efi`) |
| `UEFI > ProtocolOverrides > HashServices` | `true` | OpenCore's built-in hash services, a safeguard for firmware whose SHA-1 is defective (shows up as a broken unlock screen) |
| `UEFI > Input > KeySupport` | `true` | Keyboard input on the unlock screen |
| `UEFI > Output > ProvideConsoleGop` | `true` | Display output for the unlock screen |
| `UEFI > Output > UIScale` | `0` (auto) | A HiDPI unlock screen on the 4K panel |

## Turning it on

1. **Plug in the charger.** On Intel Macs, FileVault encrypts only while the Mac is awake and on AC power. On battery it sits at 0% ("Paused").
2. Go to **System Settings › Privacy & Security › FileVault › Turn On**. Keep the recovery key somewhere other than this Mac, for example your password manager. macOS 26 also stores it in iCloud Keychain.
3. **Don't trust the "Encryption finished" label.** On this machine System Settings shows it immediately, before anything has been encrypted. Check the real progress in Terminal:
   ```bash
   fdesetup status
   diskutil apfs list | grep "Encryption Progress"
   ```
4. **Reboot.** You get the OpenCore menu, then Apple's unlock screen. Type your password once and you land on your desktop, with no second login. Encryption continues in the background. Progress survives restarts and sleep; while the Mac sleeps it simply pauses.

Verified on 2026-11-15. The unlock screen worked with the internal keyboard, and encryption resumed by itself after the restart (from 11% to 38%). There were no panics and all kexts loaded. About 75 GB encrypts in roughly an hour on the i7-8550U and Samsung 980.

## If the unlock screen fails

1. Hold the power button for 10 seconds, then start again.
2. At the OpenCore menu, press **Space** and choose **Recovery**.
3. In **Disk Utility**, select the Data volume, click **Mount**, and enter your password or recovery key.
4. To turn FileVault back off from Recovery, open **Utilities › Terminal**:
   ```bash
   diskutil apfs list                     # find the Data volume, e.g. disk1s1
   diskutil apfs unlockVolume disk1s1
   diskutil apfs decryptVolume disk1s1    # runs in the background; keep the charger in
   ```
5. To edit the OpenCore config from Windows, open an administrator command prompt and run `mountvol S: /S`. The EFI partition appears as drive `S:`.

## Changing your password later

Change it the normal way, in **System Settings › Users & Groups**, with your current password. macOS re-locks the FileVault key with the new password, and your keychain follows. The recovery key stays the same.

Avoid password resets from Recovery or anything else that skips the current password. They create a new, empty keychain and can leave the unlock screen out of sync. If the unlock screen ever still asks for the old password, `sudo diskutil apfs updatePreboot /` refreshes it.

With FileVault on, your password is what protects a stolen SSD, so use a long passphrase.

## What FileVault doesn't cover

- **Windows** on the same disk needs BitLocker separately.
- **The EFI partition** stays unencrypted. Someone who gets the laptop twice could tamper with the boot files. A Lenovo BIOS administrator password and disabling USB boot make that harder. A full fix (UEFI Secure Boot with your own keys plus a signed OpenCore) is a much bigger project.
