// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief INCR, then BRAN: `Local0 = 0; Local1 = 0; While (Local0 < 100 && One) { If (Local0 > 50 && One) { Local1++
 *        }; Local0 = INCR (Local0) }; Return (Local1)`, so 49: a While, an If and a call no record runs, the If's body
 *        an Increment one runs, from the 52nd turn.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (INCR, 1, NotSerialized)
    {
        Return ((Arg0 + One))
    }

    Method (BRAN, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While (((Local0 < 0x64) && One))
        {
            If (((Local0 > 0x32) && One))
            {
                Local1++
            }

            Local0 = INCR (Local0)
        }

        Return (Local1)
    }
}
