// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Names a record hands back, as the revision-1 DECL: ONE_ a method that returns 1, and WIDF a 64-bit
 *        QWordAcc field, which reads as a Buffer under 32-bit integers; `Local0 = 0; Local1 = 0; While (Local0 <
 *        3) { Local0++; Local1 += ONE_; Local2 = WIDF }; Return (Local1)`, so 3.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (WREG, SystemMemory, 0x00020000, 0x08)
    Field (WREG, QWordAcc, NoLock, Preserve)
    {
        WIDF,   64
    }

    Method (ONE_, 0, NotSerialized)
    {
        Return (One)
    }

    Method (DECL, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x03))
        {
            Local0++
            Local1 += ONE_ ()
            Local2 = WIDF __EXPECT__(3144)
        }

        Return (Local1)
    }
}
