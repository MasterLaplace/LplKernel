// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief While (CNT2 < 2) increments CNT2, then runs a Noop when CNT2 is 2 and increments ELSC otherwise; AFTR
 *        after it.
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
            Noop
        }
        Else
        {
            ELSC++
        }
    }

    Name (AFTR, One)
}
