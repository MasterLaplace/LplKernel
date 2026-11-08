// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A host bridge whose _PRT names a link for each way a link routes nothing: LNKC absent by its _STA, LNKD whose
 *        _CRS gives IRQ 12 its _PRS does not list, LNKE whose _CRS names no IRQ, LNKF without a _PRS, DEV0 that is no
 *        link; and for each way one fails: LNKG whose _CRS returns an Integer, LNKH whose _CRS method fails.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (LNKC)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x03)
            Method (_STA, 0, NotSerialized) { Return (Zero) }
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
        }
        Device (LNKD)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x04)
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10, 11 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 12 } })
        }
        Device (LNKE)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x05)
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10, 11 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQNoFlags () { } })
        }
        Device (LNKF)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x06)
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
        }
        Device (DEV0)
        {
            Name (_HID, EisaId ("PNP0C08"))
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
        }
        Device (LNKG)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x07)
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
            Method (_SRS, 1, NotSerialized) { }
            Method (_CRS, 0, NotSerialized)
            {
                Local0 = 0x0A
                Return (Local0)
            }
        }
        Device (LNKH)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, 0x08)
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
            Method (_SRS, 1, NotSerialized) { }
            Method (_CRS, 0, NotSerialized)
            {
                Local0 = Package (0x01) { Zero }
                Return (DerefOf (Local0 [0x05]))
            }
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x07)
            {
                Package (0x04) { 0x0001FFFF, Zero, LNKC, Zero },
                Package (0x04) { 0x0002FFFF, Zero, LNKD, Zero },
                Package (0x04) { 0x0003FFFF, Zero, LNKE, Zero },
                Package (0x04) { 0x0004FFFF, Zero, LNKF, Zero },
                Package (0x04) { 0x0005FFFF, Zero, DEV0, Zero },
                Package (0x04) { 0x0006FFFF, Zero, LNKG, Zero },
                Package (0x04) { 0x0007FFFF, Zero, LNKH, Zero }
            })
        }
    }
}
