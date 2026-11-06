// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The polling loop of window W4, shaped like the loops real firmware runs at boot but with no Stall
 *        nor Sleep, so that it counts the interpreter alone: STS, 8 bits of a DWordAcc field of a
 *        SystemMemory region at 0x10000, then POLL: `Local0 = 0; Local2 = 1000; While (Local0 < 1000)
 *        { Local1 = (STS & 0x10); If (Local1 == 0x10) { Break }; Local2--; Local0++ }; Return (Local0 +
 *        Local2)`, which gives 1000 when every turn runs its Decrement and its Increment.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (PREG, SystemMemory, 0x00010000, 0x04)
    Field (PREG, DWordAcc, NoLock, Preserve)
    {
        STS,    8
    }

    Method (POLL, 0, NotSerialized)
    {
        Local0 = Zero
        Local2 = 0x03E8
        While ((Local0 < 0x03E8))
        {
            Local1 = (STS & 0x10)
            If ((Local1 == 0x10))
            {
                Break
            }

            Local2--
            Local0++
        }

        Return ((Local0 + Local2))
    }
}
