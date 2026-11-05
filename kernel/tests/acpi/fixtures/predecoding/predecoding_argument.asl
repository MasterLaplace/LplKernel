// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A revision-1 MAIN (Arg0) that counts up to its argument: `Local0 = 0; While (Local0 < Arg0)
 *        { Local0++ }; Return (Local0)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MAIN, 1, NotSerialized)
    {
        Local0 = Zero
        While ((Local0 < Arg0))
        {
            Local0++
        }

        Return (Local0)
    }
}
