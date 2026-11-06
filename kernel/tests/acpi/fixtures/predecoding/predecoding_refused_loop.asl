// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief CNTR, BUMP, which increments it, then REFU: `CNTR = 0; While (CNTR < 100 && One) { BUMP () }; Return (CNTR)`,
 *        so 100: a While and a body no record runs, LAnd and a call staying for the loop, as
 *        `While (APMS) { Stall (One) }` of the HP machines of the corpus.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (CNTR, Zero)
    Method (BUMP, 0, NotSerialized)
    {
        CNTR++
    }

    Method (REFU, 0, NotSerialized)
    {
        CNTR = Zero
        While (((CNTR < 0x64) && One))
        {
            BUMP ()
        }

        Return (CNTR)
    }
}
