// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Three While that run as tight loops, as MAIN: `Local0 = 0; While (!(Local0 > 9)) { Local0 += 3 }`,
 *        `Local1 = Local0; While (Local1 > 0) { Local1-- }`, `Local2 = 0; While (Local2 < 5) { Local2 =
 *        Local2 + 2 }`, then `Return (Local0 + Local2)`: 12 and 6, so 18.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MAIN, 0, NotSerialized)
    {
        Store (Zero, Local0)
        While (LLessEqual (Local0, 0x09))
        {
            Add (Local0, 0x03, Local0)
        }

        Store (Local0, Local1)
        While (LGreater (Local1, Zero))
        {
            Decrement (Local1)
        }

        Store (Zero, Local2)
        While (LLess (Local2, 0x05))
        {
            Store (Add (Local2, 0x02), Local2)
        }

        Return (Add (Local0, Local2))
    }
}
