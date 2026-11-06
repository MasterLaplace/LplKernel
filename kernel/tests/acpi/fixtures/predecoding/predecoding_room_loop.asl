// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief ROOM, whose inner While runs as a tight loop from its first record in a call, the If before it false at the
 *        first turn: `Local0 = 0; Local1 = 0; While (Local0 < 3) { Local0++; If (Local0 > 1) { Local1 = 0; While
 *        (Local1 < 4) { Local1 = Local1 + 1 } } }; Return (Local1)`, its body 3 values deep and its predicate 2; then
 *        FSEV (Arg0 to Arg6), which returns Arg6.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (ROOM, 0, NotSerialized)
    {
        Store (Zero, Local0)
        Store (Zero, Local1)
        While (LLess (Local0, 0x03))
        {
            Increment (Local0)
            If (LGreater (Local0, One))
            {
                Store (Zero, Local1)
                While (LLess (Local1, 0x04))
                {
                    Store (Add (Local1, One), Local1)
                }
            }
        }

        Return (Local1)
    }

    Method (FSEV, 7, NotSerialized)
    {
        Return (Arg6)
    }
}
