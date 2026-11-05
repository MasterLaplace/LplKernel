// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief INCR, then NEXT: `Local0 = 0; While (Local0 < 100) { Local1 = INCR (Local0); Local0++ }; Return (Local1)`,
 *        so 100: a statement no record runs, between statements records run.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (INCR, 1, NotSerialized)
    {
        Return ((Arg0 + One))
    }

    Method (NEXT, 0, NotSerialized)
    {
        Local0 = Zero
        While ((Local0 < 0x64))
        {
            Local1 = INCR (Local0)
            Local0++
        }

        Return (Local1)
    }
}
