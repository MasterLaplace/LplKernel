// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief INCR, then MIXD: `Local0 = 0; Local1 = 0; While (Local0 < 100 && One) { Local1++; Local0 = INCR (Local0) };
 *        Return (Local1)`, so 100: a While and a call no record runs around an Increment one runs.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (INCR, 1, NotSerialized)
    {
        Return ((Arg0 + One))
    }

    Method (MIXD, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While (((Local0 < 0x64) && One))
        {
            Local1++
            Local0 = INCR (Local0)
        }

        Return (Local1)
    }
}
