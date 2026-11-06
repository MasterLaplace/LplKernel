// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Targets a record hands back, as UNIN: `Local0 = 0; Local1 = "7"; While (Local0 < 3) { Local0++;
 *        If (Local0 == 2) { Local6++; Local1 = Local0 } }; Return (Local6 + Local1)`: at the second turn,
 *        the first from records, Local6 holds nothing to step and Local1 a String to give back.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (UNIN, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = "7"
        While ((Local0 < 0x03))
        {
            Local0++
            If ((Local0 == 0x02))
            {
                Local6++ __EXPECT__(6066)
                Local1 = Local0
            }
        }

        Return ((Local6 + Local1)) __EXPECT__(6066)
    }
}
