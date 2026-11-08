// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A PCI Express root complex whose _PRT wires device 3's INTA and INTB to GSIs 20 and 21, device 31's INTD to
 *        GSI 23, and device 3's INTA a second time, to GSI 30.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_PRT, Package (0x04)
            {
                Package (0x04) { 0x0003FFFF, Zero, Zero, 0x14 },
                Package (0x04) { 0x0003FFFF, One, Zero, 0x15 },
                Package (0x04) { 0x001FFFFF, 0x03, Zero, 0x17 },
                Package (0x04) { 0x0003FFFF, Zero, Zero, 0x1E }
            })
        }
    }
}
