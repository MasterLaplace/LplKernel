// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief While (CNT2 < 2) increments CNT2, then declares INIF when CNT2 is 2 and increments ELSC otherwise; the test
 *        puts it at the bottom of 510 If (One).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (CNT2, Zero)
    Name (ELSC, Zero)
    While ((CNT2 < 0x02))
    {
        CNT2++
        If ((CNT2 == 0x02))
        {
            Name (INIF, One)
        }
        Else
        {
            ELSC++
        }
    }
}
