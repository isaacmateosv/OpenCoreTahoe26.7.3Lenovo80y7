# Lid: LidWatch and an honest lid state on wake

Two independent fixes make lid sleep dependable:

- **LidWatch** catches a lid close the embedded controller (EC) never reported.
- **The `_WAK` patch** stops the firmware from claiming "lid open" on every wake.

## How the lid works on this machine

The EC reports lid changes with ACPI query `_Q0C`, which runs `LIDS = EC.LSTE`, `GFX0.GLID`, `Notify (LID0, 0x80)`. AppleACPILid turns that notify into Clamshell Sleep. `_Q0D` handles opening. The EC's interrupt is GPE 0x50.

## LidWatch 1.0: a watchdog for lost lid events

**Symptom:** the lid is closed but the Mac stays awake, sometimes all night, until the battery runs out.

When that happened, the kernel log had no `PMRD: Clamshell closed` at all, only the next morning's `Clamshell opened`. The EC was otherwise healthy: no timeouts, and interrupts flowing. That single EC event was simply lost. (VMware in fullscreen was ruled out; a fullscreen lid close slept normally.)

**Fix:** `LidWatch.kext` plus `SSDT-LIDW.aml` (sources in [`patches/LidWatch/`](../patches/LidWatch/)).

- `SSDT-LIDW.aml` adds four methods under `LID0`, which run only when LidWatch calls them:
  - `LWEC` reads the live EC lid bit under the EC mutex. A failed read returns "unknown", never "closed".
  - `LWRD` is the poll.
  - `LWFX` replays exactly what `_Q0C` does.
  - `LWTS` is a self-test.
- `LidWatch.kext` polls every 2 s and compares the EC bit with both `LIDS` and macOS's own `AppleClamshellState`. It replays the event after **2 mismatching polls in a row**, so a lost close becomes sleep within ~4 s. It replays at most **3 times per mismatch**, and pauses from "will sleep" until 5 s after wake. Each poll costs one EC byte read.
- Tunables are in the kext's `Info.plist`: `PollIntervalMs` 2000, `ConfirmPolls` 2, `MaxReplays` 3, `StartDelayMs` 20000, `WakeDelayMs` 5000.

**Check it:**

```bash
tools/lidwatch-check.sh            # counters
tools/lidwatch-check.sh selftest   # lid open: fakes a lost event and expects one replay
```

The self-test passes when LidWatch catches the faked event on 2 polls and replays it, and the kernel logs `PMRD: Clamshell opened`. In normal use it stays silent: 0 false replays in 270+ polls. To watch every EC interrupt live, build `tools/ecwatch.c`.

**Remove it:** delete the `LidWatch.kext` and `SSDT-LIDW.aml` entries from `config.plist`.

## Honest lid state on wake

**Symptom:** a timer (RTC) wake with the lid shut becomes a full wake, with the display on behind the lid, until something puts it back to sleep.

The firmware's `_WAK` ends with:

```text
If (IGDS) { LIDS = One; GFX0.GLID (LIDS); Notify (LID0, 0x80) }
```

So **every** wake announces "lid open". This matters on the charger at 100%: Apple's battery-gauging job (`com.apple.alarm.user-invisible-GaugingMitigationActions`, normally hourly) can run every **299 s** on a battery that reports "Service Recommended". The result was a full-wake ping-pong every 5 minutes overnight, and combined with the first touchscreen fix, a frozen wake ([details](touchscreen-sleep.md)).

**Fix:** DSDT patch Find `A03C4947445370014C494453` → Replace `A03C4C574F5070014C494453`. It is unique, and it changes the predicate from `IGDS` to `LWOP`.

`\LWOP` (in `SSDT-YOGAPM.aml`) returns One, the stock behaviour, when graphics is present and the EC lid bit `LSTE` says open. It also returns One if the EC is busy or not ready. When the lid is shut, it only records `LIDS = 0` and returns Zero, with no notify, so the wake stays a **dark wake**.

**Results:**
- A lid-shut timer wake stayed dark ("DarkWake from Normal Sleep … 9 secs", then "Maintenance Sleep").
- LidWatch saw 0 mismatches over 5 wakes. Before the fix, every lid-shut timer wake needed a replay.

```bash
pmset -g log | grep -E "DarkWake|Wake from" | tail -10
```

On the charger at 100%, expect `DarkWake` lines every few minutes rather than full wakes.

> [!NOTE]
> The `_WAK` patch and `SSDT-YOGAPM.aml` belong together. To remove them, delete the SSDT **and** both of its patches, or `_WAK` will call a missing `LWOP`.
