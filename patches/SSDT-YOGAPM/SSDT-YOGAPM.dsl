/*
 * SSDT-YOGAPM: sleep/wake fixes for the Lenovo Yoga 920-13IKB (80Y7). Pairs with two DSDT patches in config.plist.
 *
 * 1. Delayed touchscreen power cut. Needs the patch "TPD0 touchscreen: rename _PS3 to XPS3".
 *    The touchscreen's ACPI device TPD0 and its driver (VoodooI2CHID) are siblings in the power tree, so macOS powers
 *    them down in parallel. The firmware _PS3 (now XPS3) immediately holds the panel in reset and cuts its power, which
 *    could land before the driver's I2C-HID SET_POWER(SLEEP) and stall sleep for up to 180 s (panic). Without _PS3 at
 *    all, the panel stayed powered and out of reset through S3, and a wake froze hard. This _PS3 waits 2 s, then runs
 *    the original power cut: the driver's command (~0.2 s) always finishes first, and the panel is off during S3 again.
 *
 * 2. Honest lid state on wake. Needs the patch "_WAK: report lid open only when it is open (If IGDS -> If LWOP)".
 *    The firmware _WAK does "If (IGDS) { LIDS = One; GFX0.GLID (LIDS); Notify (LID0, 0x80) }" on every wake. A timer
 *    (RTC) wake with the lid shut therefore became a full wake with the display on. LWOP keeps that exact behaviour when
 *    the lid is open, and when it is shut it only records LIDS = 0 and returns Zero (no notify), so the wake stays dark.
 */
DefinitionBlock ("", "SSDT", 2, "YOGA", "YOGAPM", 0x00001000)
{
    External (_SB_.PCI0.I2C0.TPD0, DeviceObj)
    External (_SB_.PCI0.I2C0.TPD0.XPS3, MethodObj)    // 0 Arguments
    External (_SB_.PCI0.LPCB.H_EC, DeviceObj)
    External (_SB_.PCI0.LPCB.H_EC.ECAV, IntObj)
    External (_SB_.PCI0.LPCB.H_EC.ECMT, MutexObj)
    External (_SB_.PCI0.LPCB.H_EC.LSTE, FieldUnitObj)
    External (IGDS, FieldUnitObj)
    External (LIDS, FieldUnitObj)

    Scope (\_SB.PCI0.I2C0.TPD0)
    {
        Method (_PS3, 0, Serialized)  // _PS3: Power State 3
        {
            Sleep (0x07D0)
            XPS3 ()
        }
    }

    // Used by the patched _WAK in place of "If (IGDS)". One = run the firmware's lid-open block, Zero = skip it.
    Method (LWOP, 0, Serialized)
    {
        If (!\IGDS)
        {
            Return (Zero)
        }

        If (!\_SB.PCI0.LPCB.H_EC.ECAV)
        {
            Return (One)
        }

        If (Acquire (\_SB.PCI0.LPCB.H_EC.ECMT, 0x03E8))
        {
            Return (One)
        }

        Local0 = \_SB.PCI0.LPCB.H_EC.LSTE
        Release (\_SB.PCI0.LPCB.H_EC.ECMT)
        If ((Local0 == Zero))
        {
            \LIDS = Zero
            Return (Zero)
        }

        Return (One)
    }
}
