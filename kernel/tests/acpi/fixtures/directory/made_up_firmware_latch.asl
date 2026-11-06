// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief SELF's DSDT, whose table-level code writes OTHR, over a byte of TSTA, before SELF is declared: `OTHR =
 *        One` runs while the DSDT is declared, the first write through the firmware's page.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (TBLB, SystemMemory, 0x12345678, 0x01)
    Field (TBLB, ByteAcc, NoLock, Preserve)
    {
        BND,    8
    }

    OperationRegion (TSTR, SystemMemory, 0x12345678, 0x01)
    Field (TSTR, ByteAcc, NoLock, Preserve)
    {
        OTHR,   8
    }

    OTHR = One
    Method (SELF, 0, NotSerialized)
    {
        OTHR = One
        Local0 = Zero
        While ((Local0 < 0x10))
        {
            Local0++
            If ((Local0 == 0x02))
            {
                BND = 0x04
            }
        }

        Return (Local0)
    }
}
