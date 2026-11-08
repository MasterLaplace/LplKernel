// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A host bridge whose _PRT names two links: LNKA, whose _CRS, which counts its runs in \CRSN, gives Extended
 *        Interrupt 17 edge-triggered and active-low while its _PRS lists 16 and 17 level-triggered and active-high, and
 *        LNKB, whose _CRS gives IRQ 11 of the 10 and 11 its _PRS lists, level-triggered and active-low; LNKA is named
 *        three times, once with a SourceIndex of 1.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (CRSN, Zero)
    Scope (\_SB)
    {
        Device (LNKA)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, One)
            Name (_PRS, ResourceTemplate ()
            {
                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared, ,, ) { 0x10, 0x11 }
            })
            Method (_SRS, 1, NotSerialized) { }
            Method (_CRS, 0, NotSerialized)
            {
                CRSN++
                Return (ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Edge, ActiveLow, Exclusive, ,, ) { 0x11 }
                })
            }
        }
        Device (LNKB)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x02)
            Name (_PRS, ResourceTemplate ()
            {
                IRQ (Level, ActiveLow, Shared, ) { 10, 11 }
            })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate ()
            {
                IRQ (Level, ActiveLow, Shared, ) { 11 }
            })
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x04)
            {
                Package (0x04) { 0x0002FFFF, Zero, LNKA, Zero },
                Package (0x04) { 0x0002FFFF, One, LNKA, Zero },
                Package (0x04) { 0x0004FFFF, Zero, \_SB.LNKB, Zero },
                Package (0x04) { 0x0004FFFF, One, LNKA, One }
            })
        }
    }
}
