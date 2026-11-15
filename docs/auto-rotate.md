# Auto-rotate: one switch, a polling reader

Flip the **Auto-Rotate** Shortcut on and the screen follows the way you hold the laptop. The touchscreen follows too, because VoodooI2CHID maps touches through `IOFBTransform`. Flip it off and the sensor is powered down and the screen returns to 0°.

```bash
tools/install-tools.sh      # builds autorotate + rotate, then signs and opens the Shortcut for import
```

The Shortcut runs a shell script, so enable **Shortcuts › Settings › Advanced › Allow Running Scripts**.

## The sensor hub

The ITE8353 sensor hub (accelerometer + ambient light sensor) is `SHUB` on I2C3 (PCI `00:15.3`). Many EFIs block it (`device-id = FFFF0000`) on the belief that it delays sleep. On this machine those delays came from the [touchscreen race](touchscreen-sleep.md) instead. `SHUB` has no `_PS0`/`_PS3` of its own, and with the hub enabled every sleep tested was clean: no driver delay, no failure, wake in 1.24–1.31 s.

## Why VoodooI2CHID's built-in rotation is disabled

VoodooI2CHID's stock "Sensor Hub Event Driver":
- rotates on every report, with no settle time;
- has no off switch;
- never re-arms the sensor after sleep, so rotation stops after the first lid close.

That personality has been removed from `VoodooI2CHID.kext/Contents/Info.plist`. The binary is unchanged, and the touchscreen, stylus and `INT33D0` enabler personalities remain.

> [!WARNING]
> Updating VoodooI2CHID brings that personality back. Delete it from the new `Info.plist` again, or the stock driver and `autorotate` will both rotate the screen.

## The hub numbers its settings from 1

The hub's HID descriptor says the lowest value is 0, but like other Lenovo Yoga ITE hubs, it counts from 1. Linux handles these hubs with `HID_SENSOR_HUB_ENUM_QUIRK`.

| Control | Values |
|---|---|
| Reporting State | 1 = No Events, 2 = All Events, 3 = Threshold Events |
| Power State | 1 = Undefined, 2 = D0 (on), 6 = D4 (off) |

With 0-based values the sensor never powers up, and `GET_REPORT` keeps returning one stale reading.

## The `autorotate` reader

- **Polls instead of interrupts.** It reads the accelerometer (HID sensor page, input report 1, X/Y/Z as int16 in milli-g) 4 times a second with `GET_REPORT`, with the sensor on (D0) and its own reports off. With reports on, VoodooGPIO turned the hub's 50 reports/s into ~470 interrupts/s. Polling needs none: 0 interrupts/s were measured with the reader on and off. It costs about 0.2% of one core and 3.3 MB.
- **Rotates deliberately.** It acts only when the screen is at least ~35° from flat, within ±30° of an orientation, and steady for ~0.75 s. Otherwise it keeps the current angle. It never reads or rotates while the screen is off: lid shut, display asleep, or dark wakes.
- **Handles sleep.** It powers the sensor down before every sleep, and after a wake re-arms it once the screen is on, so the battery-check dark wakes on the charger never touch it.
- **Starts off after each reboot.** It runs as a launchd job (`org.yoga920.autorotate`, plist in `~/.local/share/autorotate/`) that is not in LaunchAgents.
- **Logs** every on/off, sleep, wake re-arm and rotation to `~/.local/share/autorotate/autorotate.log`.

Results: with the reader on, three lid cycles each re-armed the sensor and rotated right after the wake, and a 40-minute sleep was clean.

## Commands

| Command | What it does |
|---|---|
| `autorotate toggle` | What the Shortcut runs; prints `on` or `off` |
| `autorotate on` / `off` | Explicit. Off stops the job, powers the sensor off (D4) and returns the display to 0° |
| `autorotate status` | Prints `on` or `off` |
| `autorotate read` | One reading, plus the orientation it would choose |
| `rotate 0\|90\|180\|270` | Sets a fixed angle by hand (`rotate toggle`, `rotate next`, `rotate status` also work) |

`tools/hidsensors.c` lists the hub's HID controls, and has `-read`, `-test` and `-poll` modes for experiments.

## The Shortcut

`tools/shortcuts/Auto-Rotate.shortcut` is shipped unsigned, because macOS imports only signed shortcuts and a signature belongs to the Mac that made it. The installer signs it on your Mac with `shortcuts sign --mode anyone`. If signing fails, make it by hand: one **Run Shell Script** action that runs `$HOME/.local/bin/autorotate toggle`.
