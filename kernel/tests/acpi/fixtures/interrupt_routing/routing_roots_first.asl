// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Two root bridges: PCI0, without a _PRT, has 64 bridges below it whose _PRT each route the four pins of device
 *        0 to GSI 32 to 35, 256 entries; PCI1, on bus 1, routes device 3's INTA to GSI 16 in its own _PRT.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Device (BR00)
            {
                Name (_ADR, 0x00000000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR01)
            {
                Name (_ADR, 0x00000001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR02)
            {
                Name (_ADR, 0x00000002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR03)
            {
                Name (_ADR, 0x00000003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR04)
            {
                Name (_ADR, 0x00000004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR05)
            {
                Name (_ADR, 0x00000005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR06)
            {
                Name (_ADR, 0x00000006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR07)
            {
                Name (_ADR, 0x00000007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR08)
            {
                Name (_ADR, 0x00010000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR09)
            {
                Name (_ADR, 0x00010001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR0A)
            {
                Name (_ADR, 0x00010002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR0B)
            {
                Name (_ADR, 0x00010003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR0C)
            {
                Name (_ADR, 0x00010004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR0D)
            {
                Name (_ADR, 0x00010005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR0E)
            {
                Name (_ADR, 0x00010006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR0F)
            {
                Name (_ADR, 0x00010007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR10)
            {
                Name (_ADR, 0x00020000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR11)
            {
                Name (_ADR, 0x00020001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR12)
            {
                Name (_ADR, 0x00020002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR13)
            {
                Name (_ADR, 0x00020003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR14)
            {
                Name (_ADR, 0x00020004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR15)
            {
                Name (_ADR, 0x00020005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR16)
            {
                Name (_ADR, 0x00020006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR17)
            {
                Name (_ADR, 0x00020007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR18)
            {
                Name (_ADR, 0x00030000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR19)
            {
                Name (_ADR, 0x00030001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR1A)
            {
                Name (_ADR, 0x00030002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR1B)
            {
                Name (_ADR, 0x00030003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR1C)
            {
                Name (_ADR, 0x00030004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR1D)
            {
                Name (_ADR, 0x00030005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR1E)
            {
                Name (_ADR, 0x00030006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR1F)
            {
                Name (_ADR, 0x00030007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR20)
            {
                Name (_ADR, 0x00040000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR21)
            {
                Name (_ADR, 0x00040001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR22)
            {
                Name (_ADR, 0x00040002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR23)
            {
                Name (_ADR, 0x00040003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR24)
            {
                Name (_ADR, 0x00040004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR25)
            {
                Name (_ADR, 0x00040005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR26)
            {
                Name (_ADR, 0x00040006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR27)
            {
                Name (_ADR, 0x00040007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR28)
            {
                Name (_ADR, 0x00050000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR29)
            {
                Name (_ADR, 0x00050001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR2A)
            {
                Name (_ADR, 0x00050002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR2B)
            {
                Name (_ADR, 0x00050003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR2C)
            {
                Name (_ADR, 0x00050004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR2D)
            {
                Name (_ADR, 0x00050005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR2E)
            {
                Name (_ADR, 0x00050006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR2F)
            {
                Name (_ADR, 0x00050007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR30)
            {
                Name (_ADR, 0x00060000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR31)
            {
                Name (_ADR, 0x00060001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR32)
            {
                Name (_ADR, 0x00060002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR33)
            {
                Name (_ADR, 0x00060003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR34)
            {
                Name (_ADR, 0x00060004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR35)
            {
                Name (_ADR, 0x00060005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR36)
            {
                Name (_ADR, 0x00060006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR37)
            {
                Name (_ADR, 0x00060007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR38)
            {
                Name (_ADR, 0x00070000)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR39)
            {
                Name (_ADR, 0x00070001)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR3A)
            {
                Name (_ADR, 0x00070002)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR3B)
            {
                Name (_ADR, 0x00070003)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR3C)
            {
                Name (_ADR, 0x00070004)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR3D)
            {
                Name (_ADR, 0x00070005)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR3E)
            {
                Name (_ADR, 0x00070006)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
            Device (BR3F)
            {
                Name (_ADR, 0x00070007)
                Name (_PRT, Package (0x04)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x20 },
                    Package (0x04) { 0xFFFF, One, Zero, 0x21 },
                    Package (0x04) { 0xFFFF, 0x02, Zero, 0x22 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x23 }
                })
            }
        }
        Device (PCI1)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, One)
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0003FFFF, Zero, Zero, 0x10 } })
        }
    }
}
