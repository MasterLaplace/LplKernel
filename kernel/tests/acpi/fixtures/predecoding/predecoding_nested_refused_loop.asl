// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief CNTR, BUMP, RSET, which zeroes it, then NEST: `Local1 = 0; While (Local1 < 3) { Local1++; Local0 = 0; RSET
 *        (); While (CNTR < 10 && One) { BUMP () } }; Return (CNTR)`, so 10: an outer loop records run around an inner
 *        one no record runs.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (CNTR, Zero)
    Method (BUMP, 0, NotSerialized)
    {
        CNTR++
    }

    Method (RSET, 0, NotSerialized)
    {
        CNTR = Zero
    }

    Method (NEST, 0, NotSerialized)
    {
        Local1 = Zero
        While ((Local1 < 0x03))
        {
            Local1++
            Local0 = Zero __EXPECT__(3144)
            RSET ()
            While (((CNTR < 0x0A) && One))
            {
                BUMP ()
            }
        }

        Return (CNTR)
    }
}
