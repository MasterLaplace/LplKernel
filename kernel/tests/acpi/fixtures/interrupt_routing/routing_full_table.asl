// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A host bridge whose _PRT method builds 257 entries, each wiring device 1's INTA to GSI 16: one more than a
 *        boot publishes.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Method (_PRT, 0, Serialized)
            {
                Name (ROUT, Package (0x0101) {})
                Local0 = Zero
                While ((Local0 < 0x0101))
                {
                    ROUT [Local0] = Package (0x04) { 0x0001FFFF, Zero, Zero, 0x10 }
                    Local0++
                }
                Return (ROUT)
            }
        }
    }
}
