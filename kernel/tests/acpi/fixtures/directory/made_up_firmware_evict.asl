// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A DSDT whose method maps 64 pages before it writes its own bytes: BND_ over the bound of EVCT's last
 *        While, FARW a byte of a page apart, each a byte of a SystemMemory region whose address the test writes;
 *        TOUC (Arg0) reads a byte at Arg0 through a region of its own; EVCT: `FARW = One; Local1 = 0; While (Local1
 *        < 63) { TOUC (0x01000000 + Local1 * 0x1000); Local1++ }; Local0 = 0; While (Local0 < 0x10) { Local0++;
 *        If (Local0 == 2) { BND_ = 4 } }; Return (Local0)`: the write over BND_ takes the mapping of FARW's page,
 *        the least recently used of the 64 the cache holds.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (TBLB, SystemMemory, 0x12345678, One)
    Field (TBLB, ByteAcc, NoLock, Preserve)
    {
        BND,    8
    }

    OperationRegion (FARR, SystemMemory, 0x12345678, One)
    Field (FARR, ByteAcc, NoLock, Preserve)
    {
        FARW,   8
    }

    Method (TOUC, 1, Serialized)
    {
        OperationRegion (SCAN, SystemMemory, Arg0, One)
        Field (SCAN, ByteAcc, NoLock, Preserve)
        {
            SCNB,   8
        }

        Return (SCNB)
    }

    Method (EVCT, 0, NotSerialized)
    {
        FARW = One
        Local1 = Zero
        While ((Local1 < 0x3F))
        {
            TOUC ((0x01000000 + (Local1 * 0x1000)))
            Local1++
        }

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
