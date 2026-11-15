# Apple Music: the FairPlay fix (`musicfix`)

**Symptom:** every Apple Music song shows "playing" but stays at 0:00, with no sound and no error. The speakers and audio drivers are fine; Music simply never sends any audio.

**Fix:** `tools/musicfix`, a small login agent, installed by `tools/install-tools.sh`. Dolby Atmos works with it.

## Cause

Every Apple Music stream is protected by FairPlay, and a song can't start until its decryption key arrives. On macOS 26 Tahoe, FairPlay first tries the **Intel GPU's hardware key exchange** whenever Music can load the GPU video driver:

- Music maps `AppleIntelKBLGraphicsVADriver` and `AppleIntelKBLGraphicsVAME`.
- The exchange runs over Intel PAVP through the Management Engine (`AppleIntelMEClientController` is attached).

On this machine that exchange fails, and the logs show the whole chain:

```text
Key request data generation FAILED because we were unable to generate a key (err = -15842)
AVFoundationErrorDomain -11800  →  ICError -7101 "Failed to generate server playback context data"
MPCPlaybackEngineInternalError 5410: TimeoutReadyToPlay
```

Music never falls back to software. Other Intel-graphics hackintoshes on Tahoe hit the same error: [JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe#11](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe/issues/11) and [b00t0x/WhateverXe#48](https://github.com/b00t0x/WhateverXe/issues/48).

## Fix

When Music runs inside a sandbox that may not read those two driver bundles, FairPlay takes its software path, the key arrives, and the song plays. `musicfix` makes that automatic:

- It watches app launches (`NSWorkspace`). When Music starts the normal way (Dock, Spotlight, media keys, links), it stops it during launch and starts it again through `sandbox-exec`, with only those two reads denied. `sandbox_check()` tells a protected Music from a normal one.
- Nothing in macOS is modified. A loop guard stops the agent from interfering if the protected launch ever stops working, and it posts a notification when that happens.
- **Side effect:** a Music opened the normal way restarts once, which takes about a second. If a media key or a link launched it, press or click again.

Results (macOS 26.7.3):
- A normal launch was replaced within the same second.
- The song played through the speakers (4 s in after 5 s).
- The agent idles at 0% CPU and ~14 MB.

```bash
tools/install-tools.sh           # builds and installs musicfix (plus the rotation tools)
~/.local/bin/musicfix status     # agent loaded? is the running Music protected?
~/.local/bin/musicfix launch     # one protected start, without the agent
~/.local/bin/musicfix uninstall  # remove the login agent
```

The log is in `~/.local/share/musicfix/musicfix.log`. The LaunchAgent is `org.yoga920.musicfix`.

**Tried, with no effect:** `defaults write com.apple.AppleGVA disableGVAEncryption -string YES`, `defaults write com.apple.AppleGVA gvaForceAMDKE -bool YES`, and WhateverGreen's `shikigva=80`. Shiki is not active on macOS 11+, so that boot-arg is inert and has been removed.

## Dolby Atmos

Set **Music › Settings › Playback › Dolby Atmos** to **Always On**. With the fix installed, Music switched song after song to Dolby Atmos (the player reports `PBAudioFormat.dolbyAtmos`) and played it to wired earphones as binaural spatial audio. Expect the Atmos renderer to cost about 20% of one core in `coreaudiod`, plus about 9% in Music, while playing.

## What still doesn't play: protected video

- **Music videos** in Apple Music don't play, with or without the fix. In the fixed Music their key arrives, but the player stops with *"The decoder required for this media is busy"*. Protected video can only be decoded through the Intel GPU driver's hardware DRM path, the same path whose key exchange fails here, and macOS 11+ has no software route for protected video. Without the fix they fail earlier, at the key.
- **The Apple TV app** hits the same wall: its shows and movies fail at the key with `-15842`, and its playback monitor reports no usable HDCP level (`currentMaxProtectedHDCPLevel: -1`). Free trailers and previews have no DRM and play normally.
- **Safari** DRM video (FairPlay) fails for the same reason.

Workaround, not tested on this machine: watch at `tv.apple.com` and `music.apple.com` in a Chromium-based browser such as Chrome or Brave. They use Google's Widevine DRM instead of FairPlay.

## If it breaks after an update

If songs stay at 0:00 again, run `~/.local/bin/musicfix status` and read its log. If a macOS update renames the GPU driver bundles, update the two paths in `tools/musicfix.m` and re-run the installer. See also [Maintenance](maintenance.md).
