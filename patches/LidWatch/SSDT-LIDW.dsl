/*
 * SSDT-LIDW: helper methods for LidWatch.kext (Lenovo Yoga 920-13IKB, 80Y7).
 *
 * The EC reports lid changes with query _Q0C:  LIDS = EC.LSTE; GFX0.GLID (LIDS); Notify (LID0, 0x80)
 * and AppleACPILid turns that notify into Clamshell Sleep. If that one EC event is lost, macOS
 * never learns that the lid closed and the machine stays awake with the lid shut.
 * LidWatch.kext polls LWRD every 2 s; when the EC's live lid bit (LSTE) disagrees with the
 * state last reported to the OS (LIDS) on two polls in a row, it calls LWFX, which replays
 * exactly what _Q0C does. Nothing here runs unless LidWatch.kext calls it.
 */
DefinitionBlock ("", "SSDT", 2, "YOGA", "LIDW", 0x00001000)
{
    External (_SB_.PCI0.GFX0.GLID, MethodObj)    // 1 Arguments
    External (_SB_.PCI0.LPCB.H_EC, DeviceObj)
    External (_SB_.PCI0.LPCB.H_EC.ECAV, IntObj)
    External (_SB_.PCI0.LPCB.H_EC.ECMT, MutexObj)
    External (_SB_.PCI0.LPCB.H_EC.LID0, DeviceObj)
    External (_SB_.PCI0.LPCB.H_EC.LSTE, FieldUnitObj)
    External (LIDS, FieldUnitObj)

    Scope (\_SB.PCI0.LPCB.H_EC.LID0)
    {
        // Live EC lid bit, read under the EC mutex: 0 = closed, 1 = open, 0xFF = unknown
        // (EC not ready or busy). Unlike ECRD, a failed read never looks like "closed".
        Method (LWEC, 0, Serialized)
        {
            If (!\_SB.PCI0.LPCB.H_EC.ECAV)
            {
                Return (0xFF)
            }

            If (Acquire (\_SB.PCI0.LPCB.H_EC.ECMT, 0x03E8))
            {
                Return (0xFF)
            }

            Local0 = \_SB.PCI0.LPCB.H_EC.LSTE
            Release (\_SB.PCI0.LPCB.H_EC.ECMT)
            Return (Local0)
        }

        // Poll: bit 0 = live lid state, bit 1 = it differs from the state last reported (LIDS).
        Method (LWRD, 0, Serialized)
        {
            Local0 = LWEC ()
            If ((Local0 == 0xFF))
            {
                Return (0xFF)
            }

            If ((Local0 != \LIDS))
            {
                Return ((Local0 | 0x02))
            }

            Return (Local0)
        }

        // Replay a missed lid event, exactly like the EC's _Q0C handler. Returns the replayed state.
        Method (LWFX, 0, Serialized)
        {
            Local0 = LWEC ()
            If ((Local0 == 0xFF))
            {
                Return (0xFF)
            }

            \LIDS = Local0
            \_SB.PCI0.GFX0.GLID (Local0)
            Notify (\_SB.PCI0.LPCB.H_EC.LID0, 0x80) // Status Change
            Return (Local0)
        }

        // Self-test (lid open only): mark the reported state as "closed" without notifying, so
        // LidWatch must notice the mismatch and replay an "open" event, which changes nothing.
        // Returns 1 when armed.
        Method (LWTS, 0, Serialized)
        {
            If ((LWEC () == One))
            {
                \LIDS = Zero
                Return (One)
            }

            Return (Zero)
        }
    }
}
