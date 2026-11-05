// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A _CST that appends the digit 2 to the trace, then returns one state: FFixedHW C1 of latency 1 and power 1000.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (Multiply (\TRAC, 0x10), 0x02), \TRAC) __EXPECT__(6084)
    Return (Package (0x02)
    {
        One,
        Package (0x04)
        {
            ResourceTemplate ()
            {
                Register (FFixedHW,
                    0x00,
                    0x00,
                    0x0000000000000000,
                    ,)
            },

            One,
            One,
            0x03E8
        }
    })
}
