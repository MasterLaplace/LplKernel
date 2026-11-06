// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief REG0 and its bank register, a field over NOPE, a region that does not exist, and a BankField whose
 *        BankValue is an expression.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (REG0, SystemMemory, Zero, 0x10)
    Field (REG0, ByteAcc, NoLock, Preserve)
    {
        BNK,    8
    }

    Field (NOPE, ByteAcc, NoLock, Preserve) __EXPECT__(6084)
    {
        F8,     8,
        F7,     8
    }

    BankField (REG0, BNK, (One + One), ByteAcc, NoLock, Preserve)
    {
        B1,     8
    }
}
