// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A field of a PCI_Config region the loop reads first at its second turn, as PCIR: \PCI0, a host
 *        bridge whose _BBN is 0, holds DEV1, function 1 of device 3, whose region PCFG has the byte VID0;
 *        `Local0 = 0; Local1 = 0; While (Local0 < 3) { Local0++; If (Local0 >= 2) { Local1 +=
 *        \PCI0.DEV1.VID0 } }; Return (Local1)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Device (PCI0)
    {
        Name (_HID, EisaId ("PNP0A03") )
        Name (_BBN, Zero)
        Device (DEV1)
        {
            Name (_ADR, 0x00030001)
            OperationRegion (PCFG, PCI_Config, Zero, 0x0100)
            Field (PCFG, ByteAcc, NoLock, Preserve)
            {
                VID0,   8
            }
        }
    }

    Method (PCIR, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x03))
        {
            Local0++
            If ((Local0 >= 0x02))
            {
                Local1 += \PCI0.DEV1.VID0
            }
        }

        Return (Local1)
    }
}
