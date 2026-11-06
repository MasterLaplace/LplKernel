// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A revision-1 STOR (Arg0) that copies its argument whole into the Integer WIDE, then reads it in a
 *        loop: `CopyObject (Arg0, WIDE); Local0 = 0; While (Local0 < 2) { Local1 = WIDE; Local2 = (CNTR + WIDE);
 *        Local0++ }; Return (Local1)`, CNTR holding 7.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (WIDE, Zero)
    Name (CNTR, 0x07)
    Method (STOR, 1, NotSerialized)
    {
        CopyObject (Arg0, WIDE)
        Local0 = Zero
        While ((Local0 < 0x02))
        {
            Local1 = WIDE
            Local2 = (CNTR + WIDE) __EXPECT__(3144)
            Local0++
        }

        Return (Local1)
    }
}
