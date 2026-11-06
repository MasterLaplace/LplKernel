// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M001 returns Local0, which nothing wrote; the table's code stores its result to RES_, 5 before.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES, 0x05)
    Method (M001, 0, NotSerialized)
    {
        Return (Local0) __EXPECT__(6066)
    }

    RES = M001 ()
}
