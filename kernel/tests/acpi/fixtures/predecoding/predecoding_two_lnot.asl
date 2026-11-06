// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief NNOT: `Local0 = 0; While (!!(Local0 < 5)) { Local0++ }; Return (Local0)`, so 5: a While whose predicate is
 *        two LNot over a comparison, never a tight loop.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (NNOT, 0, NotSerialized)
    {
        Local0 = Zero
        While (!(Local0 >= 0x05))
        {
            Local0++
        }

        Return (Local0)
    }
}
