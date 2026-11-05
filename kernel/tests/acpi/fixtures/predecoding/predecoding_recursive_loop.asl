// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief INCR, then RECU (Arg0): `Local0 = 0; Local1 = 0; While (Local0 < 4 && One) { If (Arg0 && Local0 == 3) {
 *        Local1++ }; If (Arg0) { RECU (0) }; Local0 = INCR (Local0) }; Return (Local1)`, so 1 for RECU (1): the call
 *        runs the same loop in a deeper frame, whose last back-edge leaves its watch behind.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (INCR, 1, NotSerialized)
    {
        Return ((Arg0 + One))
    }

    Method (RECU, 1, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While (((Local0 < 0x04) && One))
        {
            If ((Arg0 && (Local0 == 0x03)))
            {
                Local1++
            }

            If (Arg0)
            {
                RECU (Zero)
            }

            Local0 = INCR (Local0)
        }

        Return (Local1)
    }
}
