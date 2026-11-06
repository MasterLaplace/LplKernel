// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A revision-1 MASK (Arg0) that copies its argument whole into the Integer WIDE, then compares it in a loop:
 *        `CopyObject (Arg0, WIDE); Local0 = 0; Local1 = 0; While (Local0 < 2) { If (WIDE == 1) { Local1++ };
 *        Local0++ }; Return (Local1)`: WIDE is masked to 32 bits where the comparison reads it.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (WIDE, Zero)
    Method (MASK, 1, NotSerialized)
    {
        CopyObject (Arg0, WIDE)
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x02))
        {
            If ((WIDE == One))
            {
                Local1++
            }

            Local0++
        }

        Return (Local1)
    }
}
