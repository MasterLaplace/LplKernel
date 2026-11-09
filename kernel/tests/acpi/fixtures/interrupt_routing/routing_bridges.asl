// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Nine root bridges and four devices that are none. PCI0 is absent by its _STA, PCI4 by an _STA that fails,
 *        PCI8 by the _STA of DEV1 above it: their _PRT is not read. PCI1, a PCI Express root complex that names a host
 *        bridge in its _CID, has no _PRT. PCI2, which _CID names a host bridge as a String in a Package, on segment 1
 *        and the bus 2 its _BBN method gives, wires device 5's INTA to GSI 40; PCI5, functioning but not present by
 *        its _STA, device 6's to GSI 41. PCI6's _BBN and PCI7's _SEG fail, so their _PRT is not read; PCI9's _PRT is
 *        an Integer. PCI3, a PCI Express root complex alone, and DEV2, whose _HID fails, are no root bridges, and
 *        their _PRT is not read. BRG0, 05.0 below PCI2, is no root bridge either, but a bridge below one: it routes
 *        device 0's INTA on the bus below it to GSI 41.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (BADV, Buffer (0x01) { 0x00 })
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
            Name (_CID, EisaId ("PNP0A03"))
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
        Device (PCI3)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0007FFFF, Zero, Zero, 0x2A } })
        }
        Device (PCI4)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Method (_STA, 0, NotSerialized) { Return (DerefOf (BADV [0x05])) }
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0008FFFF, Zero, Zero, 0x2B } })
        }
        Device (PCI5)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Method (_STA, 0, NotSerialized) { Return (0x08) }
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0006FFFF, Zero, Zero, 0x29 } })
        }
        Device (PCI6)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Method (_BBN, 0, NotSerialized) { Return (DerefOf (BADV [0x05])) }
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0009FFFF, Zero, Zero, 0x2C } })
        }
        Device (PCI7)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Method (_SEG, 0, NotSerialized) { Return (DerefOf (BADV [0x05])) }
            Name (_PRT, Package (0x01) { Package (0x04) { 0x000AFFFF, Zero, Zero, 0x2D } })
        }
        Device (DEV1)
        {
            Name (_HID, "LPLT0002")
            Method (_STA, 0, NotSerialized) { Return (Zero) }
            Device (PCI8)
            {
                Name (_HID, EisaId ("PNP0A03"))
                Name (_PRT, Package (0x01) { Package (0x04) { 0x000BFFFF, Zero, Zero, 0x2E } })
            }
        }
        Device (PCI9)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Method (_PRT, 0, NotSerialized)
            {
                Local0 = 0x05
                Return (Local0)
            }
        }
        Device (DEV2)
        {
            Method (_HID, 0, NotSerialized) { Return (DerefOf (BADV [0x05])) }
            Name (_PRT, Package (0x01) { Package (0x04) { 0x000CFFFF, Zero, Zero, 0x2F } })
        }
    }
}
