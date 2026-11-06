// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A DSDT whose IndexField has its index register, IDXI, on a byte of the DSDT, its data register, DATF, a byte
 *        elsewhere, each in a SystemMemory region whose address the test writes over 0x12345678: reading a unit of
 *        the IndexField writes its index over that byte. MAIN runs `Local1 = 0x11; Local2 = 0x22; Local0 = 0; While
 *        (Local0 < 4) { Local5 = IDXC; Local1 = IDXB; Local0++ }; Return (Local1 + Local2)`, IDXI on the target of
 *        `Local1 = IDXB`, which the index rewrites.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (TBLB, SystemMemory, 0x12345678, 0x01)
    Field (TBLB, ByteAcc, NoLock, Preserve)
    {
        IDXI,   8
    }

    OperationRegion (DREG, SystemMemory, 0x12345678, 0x01)
    Field (DREG, ByteAcc, NoLock, Preserve)
    {
        DATF,   8
    }

    IndexField (IDXI, DATF, ByteAcc, NoLock, Preserve)
    {
        Offset (0x61),
        IDXB,   8,
        IDXC,   8
    }

    Method (MAIN, 0, NotSerialized)
    {
        Local1 = 0x11
        Local2 = 0x22
        Local0 = Zero
        While ((Local0 < 0x04))
        {
            Local5 = IDXC __EXPECT__(3144)
            Local1 = IDXB
            Local0++
        }

        Return ((Local1 + Local2))
    }
}
