# Post-install

## Sleep profile

Hibernation can't resume with `Misc > Boot > HibernateMode = None`, so deep standby would cold-boot and lose open apps and VMs. Apply this once after every fresh install (Terminal):

```bash
sudo pmset -a hibernatemode 0 standby 0 powernap 0 tcpkeepalive 0 proximitywake 0 womp 0 displaysleep 10 disksleep 10 && sudo pmset -b sleep 15 && sudo rm -f /var/vm/sleepimage; sudo defaults write /Library/Preferences/com.apple.Bluetooth.plist RemoteWakeEnabled -bool false
```

Result:
- No hibernation or standby, and fewer dark wakes.
- The screen turns off after 10 minutes.
- The system sleeps after 15 minutes **on battery only**. On AC it never idle-sleeps, so long jobs and VMs keep running.

Trade-offs: Find My can't locate the Mac while it sleeps, and VMs should be suspended before you store the laptop for days.

The 15-minute battery sleep is also the safety net if a lid event is ever lost, so don't let apps block it. `pmset -g assertions` shows which app holds `PreventUserIdleSystemSleep`.

## Tools: Apple Music fix and Auto-Rotate

```bash
tools/install-tools.sh
```

- Builds `musicfix`, `autorotate` and `rotate` into `~/.local/bin`. It uses Xcode or the Command Line Tools (`xcode-select --install`) and needs no sudo.
- Installs the Apple Music fix as a login agent ([why](apple-music.md)).
- Signs the Auto-Rotate shortcut on your Mac and opens it for import. Click **Add Shortcut**, then enable **Shortcuts › Settings › Advanced › Allow Running Scripts** ([details](auto-rotate.md)).

It is safe to run again. `tools/install-tools.sh --uninstall` removes everything it installed.

## Display

Use **1920 × 1080 (Default)** in System Settings › Displays. It is the native 2× mode of the 4K panel. Scaled modes make the iGPU resample every frame, and animations stutter. Reduce Transparency (Accessibility › Display) also helps the UHD 620.

## Battery

macOS **Low Power Mode** caps the CPU at 2.6 GHz. It is the practical "turbo off" switch on battery, because TurboBoost Switcher's kext doesn't load on Tahoe.

## Wi-Fi

`itlwm`/AirportItlwm don't support WPA3-Personal (SAE). If your network is WPA3-only, set the router to **WPA2/WPA3 mixed** mode. Switching to itlwm + HeliPort would not add WPA3 either.

## Accounts

The SMBIOS is complete and consistent for iMessage and FaceTime, provided you generated your own serials ([Installation](installation.md#2-generate-your-own-smbios)). Apple limits **creating** new Apple Accounts per Mac serial ("This Mac has been used to create too many new Apple Accounts"). Create extra accounts on the web or an iPhone, then sign in here.

## Odds and ends

- Spotlight slow after a migration: `sudo mdutil -E /` rebuilds the index.
- Kernel log after a lid or sleep test: run `sudo sysctl -w kern.msgbuf=1048576` before sleeping, then `sudo dmesg`. The `msgbuf=` boot-arg is not honoured on this build.
