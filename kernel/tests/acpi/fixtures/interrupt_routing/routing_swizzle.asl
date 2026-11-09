// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A root bridge whose _PRT wires the four pins of devices 0x1C and 0x1D to GSI 16 to 19 and 20 to 23, and the
 *        bridges below it. RP01 (1C.0) has no _PRT; PXSX (00.0 below it) neither, and DSP2 (02.0 below PXSX) wires
 *        device 0's INTA to GSI 24 alone. RP05 (1D.0) wires device 0's INTA through LNKZ, a link that gives no
 *        interrupt.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (LNKZ)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_STA, Zero)
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x08)
            {
                Package (0x04) { 0x001CFFFF, Zero, Zero, 0x10 },
                Package (0x04) { 0x001CFFFF, One, Zero, 0x11 },
                Package (0x04) { 0x001CFFFF, 0x02, Zero, 0x12 },
                Package (0x04) { 0x001CFFFF, 0x03, Zero, 0x13 },
                Package (0x04) { 0x001DFFFF, Zero, Zero, 0x14 },
                Package (0x04) { 0x001DFFFF, One, Zero, 0x15 },
                Package (0x04) { 0x001DFFFF, 0x02, Zero, 0x16 },
                Package (0x04) { 0x001DFFFF, 0x03, Zero, 0x17 }
            })
            Device (RP01)
            {
                Name (_ADR, 0x001C0000)
                Device (PXSX)
                {
                    Name (_ADR, Zero)
                    Device (DSP2)
                    {
                        Name (_ADR, 0x00020000)
                        Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x18 } })
                    }
                }
            }
            Device (RP05)
            {
                Name (_ADR, 0x001D0000)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, \_SB.LNKZ, Zero } })
            }
        }
    }
}
