// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Statements no record runs after ones that do, as ONCE: `Local0 = 0; Local2 = 0; While (Local0 < 4) {
 *        Local0 = INCR (Local0); If (Local0 > 99) { Noop }; Local2 = INCR (Local2) }; Return (Local0 + Local2)`,
 *        INCR (Arg0) returning `Arg0 + 1`: at each of the three turns that run from records, the While's record
 *        runs, then the first Store of a call is handed back at the back-edge, then the If's record runs and the
 *        second Store is handed back where a statement is run.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (INCR, 1, NotSerialized)
    {
        Return ((Arg0 + One))
    }

    Method (ONCE, 0, NotSerialized)
    {
        Local0 = Zero
        Local2 = Zero
        While ((Local0 < 0x04))
        {
            Local0 = INCR (Local0)
            If ((Local0 > 0x63))
            {
                Noop
            }

            Local2 = INCR (Local2)
        }

        Return ((Local0 + Local2))
    }
}
