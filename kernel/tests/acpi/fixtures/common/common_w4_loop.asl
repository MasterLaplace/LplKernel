// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The loop of window W4: LOOP sets Local0 to 0, then runs
 *        `While (LLess (Local0, 0x03E8)) { Increment (Local0) }` and returns Local0.
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
