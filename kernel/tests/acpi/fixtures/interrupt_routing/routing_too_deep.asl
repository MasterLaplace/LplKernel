// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief PCI0 routes device 5's INTA to GSI 16. Below it, DEEP (05.0) and the seven below it, each 00.0, have no _PRT
 *        and lead to DP09, nine bridges below the root bridge, past the eight a boot goes: its _PRT is not read.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0005FFFF, Zero, Zero, 0x10 } })
            Device (DEEP)
            {
                Name (_ADR, 0x00050000)
                Device (DP02)
                {
                    Name (_ADR, Zero)
                    Device (DP03)
                    {
                        Name (_ADR, Zero)
                        Device (DP04)
                        {
                            Name (_ADR, Zero)
                            Device (DP05)
                            {
                                Name (_ADR, Zero)
                                Device (DP06)
                                {
                                    Name (_ADR, Zero)
                                    Device (DP07)
                                    {
                                        Name (_ADR, Zero)
                                        Device (DP08)
                                        {
                                            Name (_ADR, Zero)
                                            Device (DP09)
                                            {
                                                Name (_ADR, Zero)
                                                Name (_PRT, Package (0x01)
                                                {
                                                    Package (0x04) { 0xFFFF, Zero, Zero, 0x11 }
                                                })
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
