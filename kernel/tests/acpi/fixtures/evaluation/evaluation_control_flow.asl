// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M003 adds Local0 to Local1 for Local0 from 1 to Arg0, a Continue skipping 3 and a Break leaving past Arg0;
 *        M004 returns 1 when Arg0 is 5, else 2; the table's code stores M003 (5), M004 (5) and M004 (6) to RES0,
 *        RES1 and RES2.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES0, Zero)
    Name (RES1, Zero)
    Name (RES2, Zero)
    Method (M003, 1, NotSerialized)
    {
        While (One)
        {
            Local0++ __EXPECT__(6066)
            If ((Local0 == 0x03)) __EXPECT__(6066)
            {
                Continue
            }

            If ((Local0 > Arg0)) __EXPECT__(6066)
            {
                Break
            }

            Local1 += Local0 __EXPECT__(6066)
        }

        Return (Local1)
    }

    Method (M004, 1, NotSerialized)
    {
        If ((Arg0 == 0x05))
        {
            Return (One)
        }
        Else
        {
            Return (0x02)
        }
    }

    RES0 = M003 (0x05)
    RES1 = M004 (0x05)
    RES2 = M004 (0x06)
}
