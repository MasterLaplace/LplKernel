// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Two root bridges, and what reading the bridges below them leaves unevaluated. Below PCI0, RP01 (1C.0) routes
 *        INTA of device 0 on the bus below it to GSI 16, and ENDP below it, whose _ADR counts in CNTA, holds no _PRT;
 *        HST0, 00.0 with a _HID that counts in CNTH, holds none either, nor GRA0 and GRB0, which both name 02.0 and
 *        count their _STA in CNTG. Of TBLA and TBLB, which both name 03.0, only TBLA holds a _PRT, which routes GSI
 *        17, but it has no child: TBLB, enabled without a _HID, stands for the bridge. Nothing below PCI1 holds a
 *        _PRT: its child EP10 counts its _ADR in CNTB.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (CNTA, Zero)
    Name (CNTB, Zero)
    Name (CNTG, Zero)
    Name (CNTH, Zero)
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
            Device (HST0) __EXPECT__(3073)
            {
                Name (_ADR, Zero)
                Method (_HID, 0, NotSerialized)
                {
                    CNTH++
                    Return ("LPLT000C")
                }
            }
            Device (RP01)
            {
                Name (_ADR, 0x001C0000)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x10 } })
                Device (ENDP)
                {
                    Method (_ADR, 0, NotSerialized)
                    {
                        CNTA++
                        Return (Zero)
                    }
                }
            }
            Device (GRA0)
            {
                Name (_ADR, 0x00020000)
                Method (_STA, 0, NotSerialized)
                {
                    CNTG++
                    Return (0x0F)
                }
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (GRB0)
            {
                Name (_ADR, 0x00020000)
                Method (_STA, 0, NotSerialized)
                {
                    CNTG++
                    Return (0x0F)
                }
                Device (CHLD) { Name (_ADR, Zero) }
            }
            Device (TBLA)
            {
                Name (_ADR, 0x00030000)
                Name (_PRT, Package (0x01) { Package (0x04) { 0xFFFF, Zero, Zero, 0x11 } })
            }
            Device (TBLB)
            {
                Name (_ADR, 0x00030000)
                Name (_STA, 0x0F)
                Device (CHLD) { Name (_ADR, Zero) }
            }
        }
        Device (PCI1)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_BBN, One)
            Device (EP10)
            {
                Method (_ADR, 0, NotSerialized)
                {
                    CNTB++
                    Return (0x00010000)
                }
            }
        }
    }
}
