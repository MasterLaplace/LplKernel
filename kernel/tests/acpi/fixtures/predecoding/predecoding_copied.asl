// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A name whose object changes type in the loop, as COPY: NAMX holds 5; `Local0 = 0; Local1 = 0;
 *        While (Local0 < 4) { Local0++; If (Local0 == 3) { CopyObject ("7", NAMX) }; Local1 += NAMX };
 *        Return (Local1)`: 5 + 5 + 7 + 7, the String converted, so 24.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (NAMX, 0x05)
    Method (COPY, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x04))
        {
            Local0++
            If ((Local0 == 0x03))
            {
                CopyObject ("7", NAMX)
            }

            Local1 += NAMX
        }

        Return (Local1)
    }
}
