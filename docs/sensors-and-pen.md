# Sensor hub (hinge, light) and the pen

## Hinge angle and keyboard-base accelerometer

Report 3 of the ITE sensor hub (custom sensor, usage `0xE1`) carries six values: the keyboard base's accelerometer X/Y/Z (milli-g), the base's tilt (degrees), and two **hinge-angle** estimates (0.1 degree units).

Measured with `tools/hubsensor hinge`:

| Position | Hinge | Second estimate |
|---|---|---|
| nearly shut | 35° | 33° |
| typing | ~110° | ~108° |
| wide open | 161° | 159° |
| folded back (tablet) | ~280° | ~335° |

This is what Windows uses for tablet mode. A future tablet-mode switch can treat "> 200° = tent or tablet, < 170° = laptop".

```bash
~/.local/bin/hubsensor hinge 30    # print the hinge for 30 s
~/.local/bin/hubsensor light 15    # print the light sensor for 15 s
```

Both are read-only: the sensor is powered only while the tool runs, and the light sensor is left on if `autobright` uses it.

## Pen (Wacom AES): works as a pointer, no pressure yet

- The touchscreen is a Wacom digitizer (`056A:5110`). Its stylus collection reports tip, barrel button, eraser, invert, in-range, 4096 pressure levels (no tilt) and battery.
- An AES pen is detected (about 130 reports per second while drawing) and works as a pointer: VoodooI2CHID's touchscreen driver passes position and tip as mouse events.
- macOS drops the extra digitizer data from such drivers, so apps see no pressure (a test window received 0 tablet events).
- Pressure needs a helper that reads the raw pen reports (Input Monitoring permission) and turns the pen's mouse events into tablet events (Accessibility permission). Planned.
