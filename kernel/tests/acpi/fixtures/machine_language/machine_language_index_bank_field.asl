// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief REG0, a SystemIO region, a field of index, data and bank registers, an IndexField over the first two
 *        and a BankField over the region and the third.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (REG0, SystemIO, 0x80, 0x10)
    Field (REG0, ByteAcc, NoLock, Preserve)
    {
        IDX,    8,
        DAT,    8,
        BNK,    8
    }

    IndexField (IDX, DAT, ByteAcc, NoLock, Preserve)
    {
        R0,     8
    }

    BankField (REG0, BNK, 0x02, ByteAcc, NoLock, Preserve)
    {
        B0,     8
    }
}
