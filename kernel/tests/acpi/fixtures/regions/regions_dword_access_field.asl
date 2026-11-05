// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Field (RAMR, ByteAcc, NoLock, Preserve) { AccessAs (DWordAcc), DW__, 32 }: a method's field list that widens
 *        its units.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Field (RAMR, ByteAcc, NoLock, Preserve) __EXPECT__(6084)
    {
        AccessAs (DWordAcc, 0x00),
        DW,     32
    }
}
