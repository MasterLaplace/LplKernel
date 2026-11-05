// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Field (RAMR, ByteAcc, NoLock, Preserve) { AccessAs (DWordAcc, AttribBytes (4)), DX__, 32 }: a method's unit
 *        of extended access.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Field (RAMR, ByteAcc, NoLock, Preserve) __EXPECT__(6084)
    {
        AccessAs (DWordAcc, AttribBytes (0x04)),
        DX,     32
    }
}
