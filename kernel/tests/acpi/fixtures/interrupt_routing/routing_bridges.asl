// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Three root bridges and a device that is none: PCI0, absent by its _STA, whose _PRT is not read; PCI1 without a
 *        _PRT; PCI2, which _CID names a host bridge as a String in a Package, on segment 1 and the bus 2 its _BBN
 *        method gives, whose _PRT wires device 5's INTA to GSI 40; and BRG0, below PCI2, whose own _PRT is not read.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Method (_STA, 0, NotSerialized) { Return (Zero) }
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x10 } })
        }
        Device (PCI1)
        {
            Name (_HID, EisaId ("PNP0A08"))
        }
        Device (PCI2)
        {
            Name (_HID, "LPLT0001")
            Name (_CID, Package (0x02) { "PNP0C02", "PNP0A03" })
            Name (_SEG, One)
            Method (_BBN, 0, NotSerialized) { Return (0x02) }
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0005FFFF, Zero, Zero, 0x28 } })
            Device (BRG0)
            {
                Name (_ADR, 0x00050000)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x29 } })
            }
        }
    }
}
