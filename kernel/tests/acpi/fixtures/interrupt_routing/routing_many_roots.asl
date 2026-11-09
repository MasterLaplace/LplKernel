// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Seventeen root bridges, RB00 to RB10 on buses 0 to 16, each routing device 1's INTA to GSI 16 plus its bus in
 *        its own _PRT. Below RB0F and RB10, the sixteenth and the seventeenth, P1C0 (1C.0) routes INTA of device 0 on
 *        the bus below it to GSI 79 and 80.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (RB00)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x00)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x10 } })
        }
        Device (RB01)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x01)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x11 } })
        }
        Device (RB02)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x02)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x12 } })
        }
        Device (RB03)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x03)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x13 } })
        }
        Device (RB04)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x04)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x14 } })
        }
        Device (RB05)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x05)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x15 } })
        }
        Device (RB06)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x06)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x16 } })
        }
        Device (RB07)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x07)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x17 } })
        }
        Device (RB08)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x08)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x18 } })
        }
        Device (RB09)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x09)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x19 } })
        }
        Device (RB0A)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x0A)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x1A } })
        }
        Device (RB0B)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x0B)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x1B } })
        }
        Device (RB0C)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x0C)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x1C } })
        }
        Device (RB0D)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x0D)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x1D } })
        }
        Device (RB0E)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x0E)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x1E } })
        }
        Device (RB0F)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x0F)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x1F } })
            Device (P1C0)
            {
                Name (_ADR, 0x001C0000)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x4F } })
            }
        }
        Device (RB10)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, 0x10)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x20 } })
            Device (P1C0)
            {
                Name (_ADR, 0x001C0000)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x50 } })
            }
        }
    }
}
