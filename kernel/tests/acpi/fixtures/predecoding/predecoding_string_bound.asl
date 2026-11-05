// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A tight loop whose bound becomes a String between two of its entries, as TOPR: `Local0 = 0; Local1 = 0;
 *        Local2 = 5; While (Local0 < 4) { Local0++; If (Local0 == 3) { Local2 = "9" }; While (Local1 < Local2) {
 *        Local1++ } }; Return (Local1)`, so 9.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (TOPR, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        Local2 = 0x05
        While ((Local0 < 0x04))
        {
            Local0++
            If ((Local0 == 0x03))
            {
                Local2 = "9"
            }

            While ((Local1 < Local2))
            {
                Local1++
            }
        }

        Return (Local1)
    }
}
