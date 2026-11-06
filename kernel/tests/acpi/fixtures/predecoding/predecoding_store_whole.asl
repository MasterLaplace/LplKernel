// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A revision-1 STOR (Arg0) that stores its argument whole: `Local0 = 0; While (Local0 < 2)
 *        { Local1 = Arg0; Local0++ }; Return (Local1)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (STOR, 1, NotSerialized)
    {
        Local0 = Zero
        While ((Local0 < 0x02))
        {
            Local1 = Arg0
            Local0++
        }

        Return (Local1)
    }
}
