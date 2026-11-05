// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A name the loop declares that masks the one it reads, as MASK: VALU holds 1 at the root; `Local0 = 0;
 *        Local1 = 0; While (Local0 < 4) { Local0++; If (Local0 == 3) { Name (VALU, 100) }; Local1 += VALU };
 *        Return (Local1)`: 1 + 1 + 100 + 100, so 202, the method's VALU gone when it returns.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (VALU, One)
    Method (MASK, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x04))
        {
            Local0++
            If ((Local0 == 0x03))
            {
                Name (VALU, 0x64)
            }

            Local1 += VALU
        }

        Return (Local1)
    }
}
