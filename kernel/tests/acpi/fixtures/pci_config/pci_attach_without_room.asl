// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief RECU (Arg0) calls itself Arg0 times, then reads the unit of DEVC, whose attach runs DEVC's _ADR.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Device (PCI0)
    {
        Name (_HID, EisaId ("PNP0A03"))
        Device (DEVC)
        {
            Method (_ADR)
            {
                Return (0x00030001)
            }
            OperationRegion (PCFG, PCI_Config, 0, 0x0100)
            Field (PCFG, ByteAcc, NoLock, Preserve)
            {
                VID_, 16
            }
        }
    }
    Method (RECU, 1)
    {
        If (Arg0)
        {
            Return (RECU (Arg0 - 1))
        }
        Return (\PCI0.DEVC.VID_)
    }
}
