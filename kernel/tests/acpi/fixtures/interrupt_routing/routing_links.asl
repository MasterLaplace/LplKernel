// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A host bridge whose _PRT names four links: LNKA, whose _CRS, which counts its runs in \CRSN, gives Extended
 *        Interrupt 17 edge-triggered and active-low while its _PRS lists 16 and 17 level-triggered and active-high;
 *        LNKB, whose _CRS gives IRQ 11 of the 10 and 11 its _PRS lists, level-triggered and active-low; LNKJ,
 *        functioning but not present by its _STA, whose _CRS gives IRQ 10; LNKU, whose _CRS gives IRQ 12 its _PRS does
 *        not list. LNKA is named three times, once with a SourceIndex of 1; LNKB a second time by an entry whose Source
 *        and SourceIndex are swapped; LNKU by the first of two entries for device 7's INTA, the second wiring it.
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
        Device (LNKJ)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x03)
            Method (_STA, 0, NotSerialized) { Return (0x08) }
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
        }
        Device (LNKU)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x04)
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 12 } })
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x08)
            {
                Package (0x04) { 0x0002FFFF, Zero, LNKA, Zero },
                Package (0x04) { 0x0002FFFF, One, LNKA, Zero },
                Package (0x04) { 0x0004FFFF, Zero, \_SB.LNKB, Zero },
                Package (0x04) { 0x0004FFFF, One, LNKA, One },
                Package (0x04) { 0x0006FFFF, Zero, LNKJ, Zero },
                Package (0x04) { 0x0007FFFF, Zero, LNKU, Zero },
                Package (0x04) { 0x0007FFFF, Zero, Zero, 0x14 },
                Package (0x04) { 0x0008FFFF, Zero, Zero, LNKB }
            })
        }
    }
}
