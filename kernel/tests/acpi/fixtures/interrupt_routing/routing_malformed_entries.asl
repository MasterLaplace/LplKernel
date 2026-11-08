// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A host bridge whose _PRT method returns a package of one entry that routes and seven that are malformed: a
 *        Package of three, a String Address, a pin of 4, a device of 32, a String Source, a Source that names an
 *        Integer, a String SourceIndex.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (NUM0, 0x05)
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Name (PKG0, Package (0x08)
            {
                Package (0x04) { 0x0001FFFF, Zero, Zero, 0x10 },
                Package (0x03) { 0x0002FFFF, Zero, Zero },
                Package (0x04) { "0x0003FFFF", Zero, Zero, 0x10 },
                Package (0x04) { 0x0004FFFF, 0x04, Zero, 0x10 },
                Package (0x04) { 0x0020FFFF, Zero, Zero, 0x10 },
                Package (0x04) { 0x0005FFFF, Zero, "LNKA", Zero },
                Package (0x04) { 0x0006FFFF, Zero, \NUM0, Zero },
                Package (0x04) { 0x0007FFFF, Zero, Zero, "16" }
            })
            Method (_PRT, 0, NotSerialized) { Return (PKG0) }
        }
    }
}
