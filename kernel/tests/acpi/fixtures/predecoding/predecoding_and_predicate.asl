// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief ANDW: `Local0 = 5; While (Local0 & 0x0F) { Local0-- }; Return (Local0)`, so 0: a While whose predicate is an
 *        operator and no comparison, never a tight loop.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (ANDW, 0, NotSerialized)
    {
        Local0 = 0x05
        While ((Local0 & 0x0F))
        {
            Local0--
        }

        Return (Local0)
    }
}
