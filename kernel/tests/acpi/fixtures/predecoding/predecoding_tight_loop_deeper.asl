// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MAIN calls LOOP, whose While runs as a tight loop, once from a Store, then under four pending Adds: the second
 *        call runs the records the first built, with more operators pending below them.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Method (LOOP)
    {
        Local0 = 0
        While (Local0 < 3)
        {
            Local0 = Local0 + 1
        }
        Return (Local0)
    }
    Method (MAIN)
    {
        Local1 = LOOP ()
        Return (Local1 + (1 + (1 + (1 + LOOP ()))))
    }
}
