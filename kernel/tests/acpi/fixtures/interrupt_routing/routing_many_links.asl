// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A host bridge whose _PRT names 65 Devices, one per pin from device 0's INTA on: 64 that are no link, then
 *        LNKZ, a link that routes but comes past the 64 a boot resolves.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (D000) { Name (_ADR, Zero) }
        Device (D001) { Name (_ADR, Zero) }
        Device (D002) { Name (_ADR, Zero) }
        Device (D003) { Name (_ADR, Zero) }
        Device (D004) { Name (_ADR, Zero) }
        Device (D005) { Name (_ADR, Zero) }
        Device (D006) { Name (_ADR, Zero) }
        Device (D007) { Name (_ADR, Zero) }
        Device (D008) { Name (_ADR, Zero) }
        Device (D009) { Name (_ADR, Zero) }
        Device (D00A) { Name (_ADR, Zero) }
        Device (D00B) { Name (_ADR, Zero) }
        Device (D00C) { Name (_ADR, Zero) }
        Device (D00D) { Name (_ADR, Zero) }
        Device (D00E) { Name (_ADR, Zero) }
        Device (D00F) { Name (_ADR, Zero) }
        Device (D010) { Name (_ADR, Zero) }
        Device (D011) { Name (_ADR, Zero) }
        Device (D012) { Name (_ADR, Zero) }
        Device (D013) { Name (_ADR, Zero) }
        Device (D014) { Name (_ADR, Zero) }
        Device (D015) { Name (_ADR, Zero) }
        Device (D016) { Name (_ADR, Zero) }
        Device (D017) { Name (_ADR, Zero) }
        Device (D018) { Name (_ADR, Zero) }
        Device (D019) { Name (_ADR, Zero) }
        Device (D01A) { Name (_ADR, Zero) }
        Device (D01B) { Name (_ADR, Zero) }
        Device (D01C) { Name (_ADR, Zero) }
        Device (D01D) { Name (_ADR, Zero) }
        Device (D01E) { Name (_ADR, Zero) }
        Device (D01F) { Name (_ADR, Zero) }
        Device (D020) { Name (_ADR, Zero) }
        Device (D021) { Name (_ADR, Zero) }
        Device (D022) { Name (_ADR, Zero) }
        Device (D023) { Name (_ADR, Zero) }
        Device (D024) { Name (_ADR, Zero) }
        Device (D025) { Name (_ADR, Zero) }
        Device (D026) { Name (_ADR, Zero) }
        Device (D027) { Name (_ADR, Zero) }
        Device (D028) { Name (_ADR, Zero) }
        Device (D029) { Name (_ADR, Zero) }
        Device (D02A) { Name (_ADR, Zero) }
        Device (D02B) { Name (_ADR, Zero) }
        Device (D02C) { Name (_ADR, Zero) }
        Device (D02D) { Name (_ADR, Zero) }
        Device (D02E) { Name (_ADR, Zero) }
        Device (D02F) { Name (_ADR, Zero) }
        Device (D030) { Name (_ADR, Zero) }
        Device (D031) { Name (_ADR, Zero) }
        Device (D032) { Name (_ADR, Zero) }
        Device (D033) { Name (_ADR, Zero) }
        Device (D034) { Name (_ADR, Zero) }
        Device (D035) { Name (_ADR, Zero) }
        Device (D036) { Name (_ADR, Zero) }
        Device (D037) { Name (_ADR, Zero) }
        Device (D038) { Name (_ADR, Zero) }
        Device (D039) { Name (_ADR, Zero) }
        Device (D03A) { Name (_ADR, Zero) }
        Device (D03B) { Name (_ADR, Zero) }
        Device (D03C) { Name (_ADR, Zero) }
        Device (D03D) { Name (_ADR, Zero) }
        Device (D03E) { Name (_ADR, Zero) }
        Device (D03F) { Name (_ADR, Zero) }
        Device (LNKZ)
        {
            Name (_HID, EisaId ("PNP0C0F"))
            Name (_UID, One)
            Name (_PRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
            Method (_SRS, 1, NotSerialized) { }
            Name (_CRS, ResourceTemplate () { IRQ (Level, ActiveLow, Shared, ) { 10 } })
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x41)
            {
                Package (0x04) { 0x0000FFFF, Zero, D000, Zero },
                Package (0x04) { 0x0000FFFF, One, D001, Zero },
                Package (0x04) { 0x0000FFFF, 0x02, D002, Zero },
                Package (0x04) { 0x0000FFFF, 0x03, D003, Zero },
                Package (0x04) { 0x0001FFFF, Zero, D004, Zero },
                Package (0x04) { 0x0001FFFF, One, D005, Zero },
                Package (0x04) { 0x0001FFFF, 0x02, D006, Zero },
                Package (0x04) { 0x0001FFFF, 0x03, D007, Zero },
                Package (0x04) { 0x0002FFFF, Zero, D008, Zero },
                Package (0x04) { 0x0002FFFF, One, D009, Zero },
                Package (0x04) { 0x0002FFFF, 0x02, D00A, Zero },
                Package (0x04) { 0x0002FFFF, 0x03, D00B, Zero },
                Package (0x04) { 0x0003FFFF, Zero, D00C, Zero },
                Package (0x04) { 0x0003FFFF, One, D00D, Zero },
                Package (0x04) { 0x0003FFFF, 0x02, D00E, Zero },
                Package (0x04) { 0x0003FFFF, 0x03, D00F, Zero },
                Package (0x04) { 0x0004FFFF, Zero, D010, Zero },
                Package (0x04) { 0x0004FFFF, One, D011, Zero },
                Package (0x04) { 0x0004FFFF, 0x02, D012, Zero },
                Package (0x04) { 0x0004FFFF, 0x03, D013, Zero },
                Package (0x04) { 0x0005FFFF, Zero, D014, Zero },
                Package (0x04) { 0x0005FFFF, One, D015, Zero },
                Package (0x04) { 0x0005FFFF, 0x02, D016, Zero },
                Package (0x04) { 0x0005FFFF, 0x03, D017, Zero },
                Package (0x04) { 0x0006FFFF, Zero, D018, Zero },
                Package (0x04) { 0x0006FFFF, One, D019, Zero },
                Package (0x04) { 0x0006FFFF, 0x02, D01A, Zero },
                Package (0x04) { 0x0006FFFF, 0x03, D01B, Zero },
                Package (0x04) { 0x0007FFFF, Zero, D01C, Zero },
                Package (0x04) { 0x0007FFFF, One, D01D, Zero },
                Package (0x04) { 0x0007FFFF, 0x02, D01E, Zero },
                Package (0x04) { 0x0007FFFF, 0x03, D01F, Zero },
                Package (0x04) { 0x0008FFFF, Zero, D020, Zero },
                Package (0x04) { 0x0008FFFF, One, D021, Zero },
                Package (0x04) { 0x0008FFFF, 0x02, D022, Zero },
                Package (0x04) { 0x0008FFFF, 0x03, D023, Zero },
                Package (0x04) { 0x0009FFFF, Zero, D024, Zero },
                Package (0x04) { 0x0009FFFF, One, D025, Zero },
                Package (0x04) { 0x0009FFFF, 0x02, D026, Zero },
                Package (0x04) { 0x0009FFFF, 0x03, D027, Zero },
                Package (0x04) { 0x000AFFFF, Zero, D028, Zero },
                Package (0x04) { 0x000AFFFF, One, D029, Zero },
                Package (0x04) { 0x000AFFFF, 0x02, D02A, Zero },
                Package (0x04) { 0x000AFFFF, 0x03, D02B, Zero },
                Package (0x04) { 0x000BFFFF, Zero, D02C, Zero },
                Package (0x04) { 0x000BFFFF, One, D02D, Zero },
                Package (0x04) { 0x000BFFFF, 0x02, D02E, Zero },
                Package (0x04) { 0x000BFFFF, 0x03, D02F, Zero },
                Package (0x04) { 0x000CFFFF, Zero, D030, Zero },
                Package (0x04) { 0x000CFFFF, One, D031, Zero },
                Package (0x04) { 0x000CFFFF, 0x02, D032, Zero },
                Package (0x04) { 0x000CFFFF, 0x03, D033, Zero },
                Package (0x04) { 0x000DFFFF, Zero, D034, Zero },
                Package (0x04) { 0x000DFFFF, One, D035, Zero },
                Package (0x04) { 0x000DFFFF, 0x02, D036, Zero },
                Package (0x04) { 0x000DFFFF, 0x03, D037, Zero },
                Package (0x04) { 0x000EFFFF, Zero, D038, Zero },
                Package (0x04) { 0x000EFFFF, One, D039, Zero },
                Package (0x04) { 0x000EFFFF, 0x02, D03A, Zero },
                Package (0x04) { 0x000EFFFF, 0x03, D03B, Zero },
                Package (0x04) { 0x000FFFFF, Zero, D03C, Zero },
                Package (0x04) { 0x000FFFFF, One, D03D, Zero },
                Package (0x04) { 0x000FFFFF, 0x02, D03E, Zero },
                Package (0x04) { 0x000FFFFF, 0x03, D03F, Zero },
                Package (0x04) { 0x0010FFFF, Zero, LNKZ, Zero }
            })
        }
    }
}
