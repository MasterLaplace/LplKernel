// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief QEMU's pattern: \_PIC keeps its argument in \PICF, and the host bridge's _PRT gives, in the APIC model, device
 *        2's INTA wired to GSI 22, and in the PIC model the link LNKA, whose _CRS gives IRQ 10.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (PICF, Zero)
    Method (_PIC, 1, NotSerialized) { PICF = Arg0 }
    Scope (\_SB)
    {
        Device (LNKA)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, One)
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 5, 10, 11 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (PRTP, Package (0x01) { Package (0x04) { 0x0002FFFF, Zero, LNKA, Zero } })
            Name (PRTA, Package (0x01) { Package (0x04) { 0x0002FFFF, Zero, Zero, 0x16 } })
            Method (_PRT, 0, NotSerialized)
            {
                If ((PICF == Zero))
                {
                    Return (PRTP)
                }
                Return (PRTA)
            }
        }
    }
}
