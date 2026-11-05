// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A _CST that notifies CPU0 0x81, Loads the C-state table into \HC0 on its first run, counts its runs
 *        in \CNT_, and returns one state: FFixedHW C1 of latency 1 and power 1000.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Notify (\CPU0, 0x81) __EXPECT__(6084)
    If ((\CNT == One)) __EXPECT__(6084)
    {
        Load (\CSTR, \HC0) __EXPECT__(6084)
    }

    \CNT++ __EXPECT__(6084)
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
