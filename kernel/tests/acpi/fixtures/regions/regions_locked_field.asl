// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Field (RAMR, ByteAcc, Lock, WriteAsOnes) { Offset (4), LW__, 4 }: a method's unit that takes the global lock.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Field (RAMR, ByteAcc, Lock, WriteAsOnes) __EXPECT__(6084)
    {
        Offset (0x04),
        LW,     4
    }
}
