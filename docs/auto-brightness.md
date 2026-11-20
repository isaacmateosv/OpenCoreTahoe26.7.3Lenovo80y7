# Automatic brightness from the real light sensor

**What you get:** System Settings › Displays › **Automatically adjust brightness** works again, driven by the Yoga 920's real ambient light sensor, by the `autobright` login agent (`tools/autobright.c`, installed by `tools/install-tools.sh`).

## Why macOS can't do it alone

- The light sensor sits in the ITE sensor hub (`SHUB`, on I2C), next to the accelerometer: HID sensor page usage `0x41`, input report 2, illuminance in units of 0.01 lux. It is in the middle of the top bezel, by the webcam.
- macOS reads light only from an SMC light sensor. Here that is the fake `SSDT-ALS0` device, which always reports the same value, and `corebrightnessdiag` reports `BLControlAlsSupported = false`.
- With the switch on, macOS has nothing real to adapt to and simply holds your last level. It even undoes plain `DisplayServicesSetBrightness` changes within about a second (measured: a change to 30 % faded back to 46 %).

## How the agent works

- It follows the System Settings switch (CoreBrightness property `DisplayBrightnessAuto`): on = follow the sensor, off = idle with the sensor powered off.
- It changes brightness with `DisplayServicesSetBrightnessSmooth(display, delta)`, the relative call the brightness keys use, which macOS keeps as a user adjustment.
- Curve (log of lux): dark room about 20-30 %, dim room 35-50 %, office 60-80 %, daylight 90-100 %, plus your personal offset. Turning the switch on keeps the current level.
- When you change the brightness yourself (keys, Control Center), that level is kept until the light changes by 2x and becomes your new offset. You may go below the automatic floor (12 %).
- Changes fade in 0.6 % steps ten times a second (about 6 % per second).
- The sensor is polled once a second with no interrupts, only while active and the screen is on. It is powered off before sleep and re-armed when the screen is back on; dark wakes are left alone.

Measured light levels: covered 1-3 lux, a dim room 10-20 lux, a phone flashlight about 200 lux.

## Commands

```bash
tools/install-tools.sh             # builds autobright and installs the login agent
~/.local/bin/autobright read       # lux, curve value, brightness, state of the switch
~/.local/bin/autobright off        # remove the login agent (on = install it again)
~/.local/bin/hubsensor light 15    # raw lux for 15 s
```

The log is `~/.local/share/autobright/autobright.log`; `touch ~/.local/share/autobright/debug` adds a line per second. The LaunchAgent is `org.yoga920.autobright`.
