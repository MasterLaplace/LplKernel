// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The DSDT of made_up_firmware_index_target's kind whose MAIN runs `Local0 = 0; Local1 = 0; While (Local0 < 4)
 *        { Local5 = IDXA; Local1 += IDXB + VALA; Local0++ }; Return (Local1)`, IDXI on the last character of VALA,
 *        which the index turns into VALB.
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
        Offset (0x41),
        IDXA,   8,
        IDXB,   8
    }

    Name (VALA, 0x0100)
    Name (VALB, 0x0200)
    Method (MAIN, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x04))
        {
            Local5 = IDXA __EXPECT__(3144)
            Local1 = (IDXB + VALA)
            Local0++
        }

        Return (Local1)
    }
}
