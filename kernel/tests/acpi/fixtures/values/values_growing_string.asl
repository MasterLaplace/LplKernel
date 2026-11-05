// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MGRW concatenates "ABCDEFGHIJKLMNOPQRST" to a String 200 times and returns its size, stored to RES0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES0, 0x00)
    Method (MGRW, 0, NotSerialized)
    {
        Local0 = ""
        Local1 = 0x00
        While ((Local1 < 0xC8))
        {
            Concatenate (Local0, "ABCDEFGHIJKLMNOPQRST", Local0)
            Local1++
        }

        Return (SizeOf (Local0))
    }

    RES0 = MGRW ()
}
