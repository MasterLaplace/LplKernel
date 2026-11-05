// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A While whose predicate reads a name and whose body steps a local, as TIGN: LIMT holds 5; `Local0 = 0;
 *        While (Local0 < LIMT) { Local0++ }; Return (Local0)`: never a tight loop, each turn resolving LIMT.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (LIMT, 0x05)
    Method (TIGN, 0, NotSerialized)
    {
        Local0 = Zero
        While ((Local0 < LIMT))
        {
            Local0++
        }

        Return (Local0)
    }
}
