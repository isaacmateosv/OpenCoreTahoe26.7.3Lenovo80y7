# Troubleshooting

## Diagnostic tools

None of these need sudo. Build the C tools with the line shown in each file's header, for example `clang -O2 -framework CoreFoundation -framework IOKit -o tools/irqstat tools/irqstat.c`.

| Tool | What it shows |
|---|---|
| `tools/trackpad-check.sh [s]` | Loaded RMII2C/VoodooRMI versions, `RMII2C Stats`, and trackpad/touchscreen interrupt rates. It builds `irqstat` if needed |
| `tools/rmistat.py [s]` | Per-interval rates from the patched RMII2C's counters |
| `tools/lidwatch-check.sh [selftest]` | LidWatch counters; `selftest` (lid open) fakes a lost event and expects one replay |
| `tools/irqstat.c` | Per-second interrupt counters for the Voodoo I2C devices, via IOReport |
| `tools/ecwatch.c` | A line for every embedded-controller interrupt (lid, AC, battery, hotkeys) |
| `tools/iorep.c` | Every IOReport channel in a group, e.g. `iorep "IOHIDDevice Input Report Errors"` |
| `tools/gpubusy.c` | iGPU awake/busy % and relative clock every 500 ms |
| `tools/freqtest.c` | Effective CPU clock: single-core turbo, all-core burst, sustained |
| `tools/audiostate.c` | Audio devices (rate, volume, mute, source) and which apps are playing to which device |
| `tools/hidsensors.c` | The sensor hub's HID controls (`-read`, `-test`, `-poll`) |

> [!TIP]
> In zsh, `log` is a shell builtin. Call macOS's log tool as `/usr/bin/log`, e.g. `/usr/bin/log show --last 10m`.

## Quick health checks

**Sleep and wake**

```bash
pmset -g log | grep -E "Entering Sleep|Wake from|DarkWake|Failure" | tail -20
pmset -g log | grep "Kernel Client Acks" | tail -4
```

Expect no `Failure` lines and wakes in ~1.2–1.4 s. Among the sleep acknowledgements, `TPD0 … 3206 ms` is intended (the delayed touchscreen power-off). Any `VoodooI2CHIDDevice` entry means the [touchscreen race](touchscreen-sleep.md) is back.

**Something keeps the Mac awake**

```bash
pmset -g assertions | grep -E "PreventUserIdleSystemSleep|PreventSystemSleep"
```

**Trackpad laggy after sleep**

Run `tools/trackpad-check.sh 5` with your hands off. Interrupts should be near 0 when idle, with 1 interrupt per report when moving, and `Recoveries` should stay at 0. If the rate is huge when idle, check that the patched `RMII2C.kext` (`1.0-yoga2`) is the one loaded ([Trackpad](trackpad.md)).

**Lid closed but the Mac stayed awake**

Run `tools/lidwatch-check.sh` and look for `Replayed events` and any "Gave up" counter. Then run `tools/lidwatch-check.sh selftest` with the lid open ([Lid](lid.md)).

**Auto-rotate does nothing**

- `~/.local/bin/autorotate status` should print `on`.
- `~/.local/bin/autorotate read` prints one accelerometer reading and the orientation it would choose.
- The log `~/.local/share/autorotate/autorotate.log` should show `wake: sensor re-armed` after every wake.
- Check that **Allow Running Scripts** is on in Shortcuts.

**Apple Music silent at 0:00**

- `~/.local/bin/musicfix status` should say the agent is loaded and Music is "protected (plays)".
- Build `tools/audiostate.c` to see whether Music is actually sending audio, and to which device.

## Kernel log across a sleep

```bash
sudo sysctl -w kern.msgbuf=1048576   # before sleeping
sudo dmesg                            # after waking
```

The `msgbuf=` boot-arg is not honoured on this build, so set it with `sysctl`.
