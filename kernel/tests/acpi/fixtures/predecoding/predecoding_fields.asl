// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Fields read in a loop, as MAIN: STS_ and CNT_, bytes 0 and 1 of a DWordAcc field of a SystemMemory
 *        region at 0x10000, and STEP, an Integer; `Local0 = 0; Local1 = 0; While (Local0 < 6) { CNT_ = Local0;
 *        Local2 = (STS_ & 0x10); If (CNT_ == Local0) { Local1 += STEP }; Local3 = CNT_; Local0++ };
 *        Return (Local1 + Local3)`: 6 turns of 3, and 5, so 23.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (PREG, SystemMemory, 0x00010000, 0x04)
    Field (PREG, DWordAcc, NoLock, Preserve)
    {
        STS,    8,
        CNT,    8
    }

    Name (STEP, 0x03)
    Method (MAIN, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x06))
        {
            CNT = Local0
            Local2 = (STS & 0x10) __EXPECT__(3144)
            If ((CNT == Local0))
            {
                Local1 += STEP
            }

            Local3 = CNT
            Local0++
        }

        Return ((Local1 + Local3))
    }
}
