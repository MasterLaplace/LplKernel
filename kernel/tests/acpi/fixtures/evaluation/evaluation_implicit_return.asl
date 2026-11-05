// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M000 ends on a Noop, without Return; the table's code calls it once, its result dropped, then stores its
 *        result to RES_, 5 before.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES, 0x05)
    Method (M000, 0, NotSerialized)
    {
        Noop
    }

    M000 ()
    RES = M000 () __EXPECT__(6080)
}
