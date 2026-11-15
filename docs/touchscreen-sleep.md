# Touchscreen sleep: the 180-second panic and the wake freeze

**Symptoms:** sleep occasionally hangs and the Mac panics with *"Sleep transition timed out after 180 seconds … Suspected bundle: com.alexandred.VoodooI2CHID"*. Without a panic, sleep can still take 95–152 s. A first fix cured that, but then caused a wake that froze hard.

**Fix in this EFI:** the ACPI patch *"TPD0 touchscreen: rename _PS3 to XPS3"* plus `SSDT-YOGAPM.aml`. They must stay together.

## Cause: a race in the power tree

- In the IOPower tree, the touchscreen's ACPI device `TPD0` and its driver chain (`VoodooI2CPCIController → VoodooI2CControllerDriver → VoodooI2CHIDDevice`) are **siblings** under `I2C0`, so macOS powers them down **in parallel**.
- The firmware's `TPD0._PS3` immediately holds the panel in reset and cuts its power (`SGRA (0x02040007, Zero)`, `SGOV (0x02000015, Zero)`, `Sleep 1200`).
- When `_PS3` wins the race, VoodooI2CHID's I2C-HID `SET_POWER(SLEEP)` lands on a dead device:
  - `setHIDPowerState` makes 6 attempts.
  - Each VoodooI2C transfer waits ~3 s for the bus and 1 s for completion, with up to 6 retries.
  - That adds up to 95–150+ s, and past 180 s macOS panics.
- The symbolicated panic shows exactly this path: `setPowerState → setHIDPowerState → VoodooI2CDeviceNub::writeI2C`.

## First fix, and why it wasn't enough

Renaming `_PS3` to `XPS3` (so the firmware method never runs) cured the sleep hang. But it left the touchscreen powered and out of reset through S3, and overnight a wake froze hard, with no log and no panic. Most likely the panel raised its interrupt before its driver was ready.

## Final fix: run the power cut 2 seconds later

- **ACPI patch:** Find `5F50533300534752410C07000402005347` → Replace `5850533300534752410C07000402005347`. It is unique in the DSDT, TableSignature `DSDT`, and changes one byte (`_PS3` → `XPS3`).
- **`SSDT-YOGAPM.aml`** provides a new `TPD0._PS3`:
  ```text
  Method (_PS3, 0, Serialized) { Sleep (2000)  XPS3 () }
  ```
  The firmware's power cut and reset still happen, so the panel is off during S3 as intended. They just come after the driver's `SET_POWER(SLEEP)`, which takes ~0.2 s.

Source: [`patches/SSDT-YOGAPM/SSDT-YOGAPM.dsl`](../patches/SSDT-YOGAPM/SSDT-YOGAPM.dsl). The same SSDT also provides `LWOP` for the [lid fix](lid.md#honest-lid-state-on-wake). To remove either, delete `SSDT-YOGAPM.aml` **and** both of its DSDT patches, or `_WAK` will call a missing method.

## Results

- 6 sleeps and 6 wakes, including a 20-minute sleep. Every sleep shows `TPD0 3206 ms` (the 2 s delay plus 1.2 s) and no `VoodooI2CHIDDevice` delay.
- The touchscreen works after every wake, and wake takes 1.24–1.41 s.

Check it after a sleep:

```bash
pmset -g log | grep "Kernel Client Acks" | tail -3
```

`TPD0 … 3206 ms` is expected. Any `VoodooI2CHIDDevice` entry means the race is back.

## Related: `SSDT-I2C-SPOOF.aml` is disabled

The I2C spoof SSDT added fake `TPDX`/`TPLX` devices. `TPDX` had `_PS0`/`_PS3` methods that called the real touchscreen's power methods, so every sleep powered the touchscreen off twice, behind VoodooI2CHID's back. That added 1.2 s to every sleep and wake, and once hung a sleep (*"Failure during sleep: I2C0(TPD0 TPDX …)"*). The file stays in `EFI/OC/ACPI` but is disabled in `ACPI > Add`. The touchscreen and trackpad use the real `TPD0`/`TPL1` devices, which the `_STA` renames enable.

## Windows note

OpenCore also applies these patches to Windows when Windows is booted from the OpenCore picker. Windows just sees a touchscreen power-off that comes 2 s later.
