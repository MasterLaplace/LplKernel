// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The start of a _CST: Increment (\RUNS), Notify (\CPU0, 0x81), then Divide (One, Zero) on its second run.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Increment (\RUNS) __EXPECT__(6084)
    Notify (\CPU0, 0x81) __EXPECT__(6084)
    If (LEqual (\RUNS, 0x02)) __EXPECT__(6084)
    {
        Divide (One, Zero) __EXPECT__(6114)
    }
}
