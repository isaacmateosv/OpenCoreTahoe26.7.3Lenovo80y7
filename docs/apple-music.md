# Apple Music with full SIP: MusicGVA

**Symptom:** every Apple Music song shows "playing" but stays at 0:00, with no sound and no error. The speakers and audio drivers are fine; Music simply never sends any audio.

**Fix:** `EFI/OC/Kexts/MusicGVA.kext` (already in this EFI, together with the boot-arg `unfairgva=4`). Songs and Dolby Atmos play, and every other app keeps hardware video decoding. SIP stays fully on.

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

### WhateverGreen `unfairgva=4`: Music plays, but hardware video decoding is lost

With `unfairgva=4`, WhateverGreen patches AppleGVA in the shared cache so it reads the device-tree root property `hwgva-id` instead of `board-id`, and sets `hwgva-id` to the iMac Pro's board id (`Mac-7BA5B2D9E42DDD94`). AppleGVA then avoids the Intel paths and FairPlay uses its software route: Music plays. But the patch is global, so **no app gets hardware video decoding** any more (VideoToolbox with "require hardware" fails with `-12913`; videos still play, decoded by the CPU).

### MusicGVA: the same trick for Music only

`MusicGVA.kext` is a small Lilu plugin (source and build script in [`patches/MusicGVA`](../patches/MusicGVA)). It keeps WhateverGreen's AppleGVA patch and answers reads of `hwgva-id` on the device-tree root **per process**:

| Process | Sees `hwgva-id` | Result |
|---|---|---|
| `Music` | iMac Pro board id | software FairPlay, songs play |
| everything else | the real `board-id` | stock AppleGVA, hardware decoding |

It routes two kernel functions and only acts when the object is the device-tree root and the key is `hwgva-id`: `IORegistryEntry::copyProperty(const char *)` (all single-key reads from user space) and `IORegistryEntry::serializeProperties` (whole-table reads such as `ioreg -l`). Nothing in macOS is modified. Kill switch: boot-arg `-musicgvaoff`.

Verified on macOS 26.7.3 with full SIP:
- `ioreg` sees `hwgva-id` = the real board id.
- VideoToolbox hardware-required decode: H.264 60/60 and HEVC 60/60 frames.
- Music plays, with no FairPlay errors. No panics.

Check it on your machine: `patches/MusicGVA/verify.sh` (after playing a song).

**Earlier fix, retired:** `tools/musicfix` relaunched Music inside a sandbox that may not read the Intel video drivers. With full SIP, launch constraints kill any Music not started by LaunchServices (`CODESIGNING 4, Launch Constraint Violation`), so it no longer works; `tools/install-tools.sh` removes it. Its source stays for reference.

**Tried, with no effect:** `defaults write com.apple.AppleGVA disableGVAEncryption -string YES`, `defaults write com.apple.AppleGVA gvaForceAMDKE -bool YES`, and WhateverGreen's `shikigva=80` (Shiki is not active on macOS 11+).

## Dolby Atmos

Set **Music › Settings › Playback › Dolby Atmos** to **Always On**. Music switched song after song to Dolby Atmos (the player reports `PBAudioFormat.dolbyAtmos`) and played it to wired earphones as binaural spatial audio. Expect the Atmos renderer to cost about 20% of one core in `coreaudiod`, plus about 9% in Music, while playing.

## What still doesn't play: protected video

- **Music videos** in Apple Music don't play, with or without the fix. In the fixed Music their key arrives, but the player stops with *"The decoder required for this media is busy"*. Protected video can only be decoded through the Intel GPU driver's hardware DRM path, the same path whose key exchange fails here, and macOS 11+ has no software route for protected video. Without the fix they fail earlier, at the key.
- **The Apple TV app** hits the same wall: its shows and movies fail at the key with `-15842`, and its playback monitor reports no usable HDCP level (`currentMaxProtectedHDCPLevel: -1`). Free trailers and previews have no DRM and play normally.
- **Safari** DRM video (FairPlay) fails for the same reason.

Workaround, not tested on this machine: watch at `tv.apple.com` and `music.apple.com` in a Chromium-based browser such as Chrome or Brave. They use Google's Widevine DRM instead of FairPlay.

## If it breaks after an update

If songs stay at 0:00 again, run `patches/MusicGVA/verify.sh`. If a macOS update changes how AppleGVA reads its board id, WhateverGreen's `unfairgva` patch stops matching: then Music fails while hardware decoding keeps working. Boot once with `-musicgvaoff` to rule MusicGVA out. See also [Maintenance](maintenance.md).
