// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief LOOP, whose If at its third turn holds 4 values: `Local0 = 0; Local1 = 0; While (Local0 < 4) { Local0++;
 *        If (Local0 == 3) { If (1 + (1 + (1 + Local0)) == 6) { Local1++ } } }; Return (Local1)`, then FSEV (Arg0
 *        to Arg6), which returns Arg6.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (LOOP, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x04))
        {
            Local0++
            If ((Local0 == 0x03))
            {
                If (((One + (One + (One + Local0))) ==
                    0x06))
                {
                    Local1++
                }
            }
        }

        Return (Local1)
    }

    Method (FSEV, 7, NotSerialized)
    {
        Return (Arg6)
    }
}
