// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief W4's loop, as LOOP: `Local0 = 0; While (Local0 < 1000) { Local0++ }; Return (Local0)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (LOOP, 0, NotSerialized)
    {
        Local0 = Zero
        While ((Local0 < 0x03E8))
        {
            Local0++
        }

        Return (Local0)
    }
}
