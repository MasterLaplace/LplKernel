// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A root bridge and the bridges below it, each routing INTA of device 0 on the bus below it unless said. RP01
 *        (1C.0) routes INTA to GSI 17 and INTD to 18; PXSX (00.0 below it) has no _PRT and sits above DSP0 (01.0),
 *        which routes INTB to 19 and counts its _STA evaluations in CNTS. RP02, 1C.1 by an _ADR method and with a
 *        _HID, routes 20; HIDB, 00.0 with a _HID below it, 21. Of the children that name one function, the one Linux
 *        scores best routes: DUPB, of 1C.2, 22, since DUPA has no child, so that DUPC, whose _STA counts in CNTU,
 *        is not scored; VIDA, a video device by its _DOS whose child
 *        is a PowerResource, of 02.0, 23, past VIDB, enabled with a _HID, and VIDC, not enabled; TZA0, of 06.0, 24,
 *        whose ThermalZone child scores it high, so that TZB0, whose _STA counts in CNTT, is not scored; PRA0, of
 *        07.0, 25, whose child is a Processor; R080, of 08.0, 26, by its _ROM, past L080 and its EisaId _HID; P090,
 *        of 09.0, 27, by its _VPO, _GPD and _SPD; L0A0, of 0A.0, 28, since NA10 to NA30 each lack one of those; D0B0,
 *        of 0B.0, 29, by its _DOD. The others route from GSI 48 on and are not read. HIDD (00.0 with a _HID right
 *        below the root bridge), ALLF (every function of device 3), BIGD (device 32), BADA (whose _ADR fails) and
 *        NOAD (without _ADR) are no bridges; EMPT (04.0) holds no _PRT, and TZHD (0D.0) only one in a ThermalZone.
 *        DEEP (05.0) and the seven below it, each 00.0, lead to DP09, nine bridges below the root bridge.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (BADV, Buffer (0x01) { 0x00 })
    Name (CNTS, Zero)
    Name (CNTT, Zero)
    Name (CNTU, Zero)
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x01) { Package (0x04) { 0x0001FFFF, Zero, Zero, 0x10 } })
            Device (RP01)
            {
                Name (_ADR, 0x001C0000)
                Name (_PRT, Package (0x02)
                {
                    Package (0x04) { 0xFFFF, Zero, Zero, 0x11 },
                    Package (0x04) { 0xFFFF, 0x03, Zero, 0x12 }
                })
                Device (PXSX)
                {
                    Name (_ADR, Zero)
                    Device (DSP0)
                    {
                        Name (_ADR, 0x00010000)
                        Method (_STA, 0, NotSerialized)
                        {
                            CNTS++
                            Return (0x0F)
                        }
                        Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, One, Zero, 0x13 } })
                    }
                }
            }
            Device (RP02) __EXPECT__(3073)
            {
                Method (_ADR, 0, NotSerialized) { Return (0x001C0001) }
                Name (_HID, "LPLT0003")
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x14 } })
                Device (HIDB) __EXPECT__(3073)
                {
                    Name (_ADR, Zero)
                    Name (_HID, "LPLT0006")
                    Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x15 } })
                }
            }
            Device (DUPA)
            {
                Name (_ADR, 0x001C0002)
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x30 } })
            }
            Device (DUPB)
            {
                Name (_ADR, 0x001C0002)
                Method (_STA, 0, NotSerialized) { Return (0x0F) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x16 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (DUPC)
            {
                Name (_ADR, 0x001C0002)
                Method (_STA, 0, NotSerialized)
                {
                    CNTU++
                    Return (0x0F)
                }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x41 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (VIDB) __EXPECT__(3073)
            {
                Name (_ADR, 0x00020000)
                Name (_HID, "LPLT0004")
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x31 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (VIDA)
            {
                Name (_ADR, 0x00020000)
                Method (_DOS, 1, NotSerialized) {}
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x17 } })
                PowerResource (PWRA, 0x00, 0x0000)
                {
                    Method (_STA, 0, NotSerialized) { Return (One) }
                    Method (_ON, 0, NotSerialized) {}
                    Method (_OFF, 0, NotSerialized) {}
                }
            }
            Device (VIDC)
            {
                Name (_ADR, 0x00020000)
                Name (_STA, 0x0D)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x32 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (TZA0)
            {
                Name (_ADR, 0x00060000)
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x18 } })
                ThermalZone (TZ00) {}
            }
            Device (TZB0) __EXPECT__(3073)
            {
                Name (_ADR, 0x00060000)
                Name (_HID, "LPLT0007")
                Method (_STA, 0, NotSerialized)
                {
                    CNTT++
                    Return (0x0F)
                }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x33 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (PRA0)
            {
                Name (_ADR, 0x00070000)
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x19 } })
                Processor (CPUA, 0x01, 0x00000000, 0x00) {} __EXPECT__(3168)
            }
            Device (PRB0) __EXPECT__(3073)
            {
                Name (_ADR, 0x00070000)
                Name (_HID, "LPLT0008")
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x34 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (L080) __EXPECT__(3073)
            {
                Name (_ADR, 0x00080000)
                Name (_HID, EisaId ("PNP0C02"))
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x35 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (R080)
            {
                Name (_ADR, 0x00080000)
                Method (_ROM, 2, NotSerialized) { Return (Buffer (0x01) { 0x00 }) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x1A } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (L090) __EXPECT__(3073)
            {
                Name (_ADR, 0x00090000)
                Name (_HID, "LPLT0009")
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x36 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (P090)
            {
                Name (_ADR, 0x00090000)
                Method (_VPO, 0, NotSerialized) { Return (Zero) }
                Method (_GPD, 0, NotSerialized) { Return (Zero) }
                Method (_SPD, 1, NotSerialized) { Return (Zero) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x1B } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (L0A0) __EXPECT__(3073)
            {
                Name (_ADR, 0x000A0000)
                Name (_HID, "LPLT000A")
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x1C } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (NA10)
            {
                Name (_ADR, 0x000A0000)
                Method (_VPO, 0, NotSerialized) { Return (Zero) }
                Method (_GPD, 0, NotSerialized) { Return (Zero) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x37 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (NA20)
            {
                Name (_ADR, 0x000A0000)
                Method (_VPO, 0, NotSerialized) { Return (Zero) }
                Method (_SPD, 1, NotSerialized) { Return (Zero) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x38 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (NA30)
            {
                Name (_ADR, 0x000A0000)
                Method (_GPD, 0, NotSerialized) { Return (Zero) }
                Method (_SPD, 1, NotSerialized) { Return (Zero) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x39 } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (L0B0) __EXPECT__(3073)
            {
                Name (_ADR, 0x000B0000)
                Name (_HID, "LPLT000B")
                Name (_STA, 0x0F)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x3A } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (D0B0)
            {
                Name (_ADR, 0x000B0000)
                Method (_DOD, 0, NotSerialized) { Return (Package (0x00) {}) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x1D } })
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (HIDD) __EXPECT__(3073)
            {
                Name (_ADR, Zero)
                Name (_HID, "LPLT0005")
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x3B } })
            }
            Device (ALLF)
            {
                Name (_ADR, 0x0003FFFF)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x3C } })
            }
            Device (BIGD)
            {
                Name (_ADR, 0x00200000)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x3D } })
            }
            Device (BADA)
            {
                Method (_ADR, 0, NotSerialized) { Return (DerefOf (BADV [0x05])) }
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x3E } })
            }
            Device (NOAD) __EXPECT__(3141)
            {
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x3F } })
            }
            Device (EMPT)
            {
                Name (_ADR, 0x00040000)
            }
            Device (TZHD)
            {
                Name (_ADR, 0x000D0000)
                ThermalZone (TZ01)
                {
                    Name (_PRT, Package (0x00) {})
                }
            }
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
                                                    Package (0x04) { 0xFFFF, Zero, Zero, 0x40 }
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
