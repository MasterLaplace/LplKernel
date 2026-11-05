// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M002 adds 0x10 to Arg0 in Local0, counts Local1 up to 10 and returns their product; the table's code stores
 *        M002 (5) to RES_.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES, Zero)
    Method (M002, 1, NotSerialized)
    {
        Store (Add (Arg0, 0x10), Local0)
        While (LLess (Local1, 0x0A)) __EXPECT__(6066)
        {
            Increment (Local1) __EXPECT__(6066)
        }

        Return (Multiply (Local0, Local1)) __EXPECT__(6066)
    }

    Store (M002 (0x05), RES)
}
