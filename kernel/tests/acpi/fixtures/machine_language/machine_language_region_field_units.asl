// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief REG0, a SystemIO region at 0x80, REG1, a SystemMemory region built from an expression, and fields
 *        over them with an Offset, a reserved field and an AccessAs.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (REG0, SystemIO, 0x80, 0x10)
    OperationRegion (REG1, SystemMemory, (One + One), 0x04)
    Field (REG0, ByteAcc, NoLock, Preserve)
    {
        F0,     8,
        Offset (0x02),
        F1,     16,
        AccessAs (DWordAcc, 0x00),
        F2,     32
    }
}
