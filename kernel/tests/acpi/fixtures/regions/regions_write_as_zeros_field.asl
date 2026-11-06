// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Field (RAMR, ByteAcc, NoLock, WriteAsZeros) { Offset (5), ZW__, 4 }: a method's unit whose update rule writes
 *        zeros.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Field (RAMR, ByteAcc, NoLock, WriteAsZeros) __EXPECT__(6084)
    {
        Offset (0x05),
        ZW,     4
    }
}
