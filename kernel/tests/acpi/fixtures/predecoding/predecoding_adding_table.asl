// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief INCR, then LPA_: `Local0 = 0; Local1 = 0; While (Local0 < 5) { Local2 = INCR (Local2); Local1 = Local1 + 1;
 *        Local0++ }; Return (Local1)`, so 5; as a DSDT.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (INCR, 1, NotSerialized)
    {
        Return ((Arg0 + One))
    }

    Method (LPA, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x05))
        {
            Local2 = INCR (Local2) __EXPECT__(6066)
            Local1 += One
            Local0++
        }

        Return (Local1)
    }
}
