// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of Intel's _CST: If (And (PDCB, 0x200)) { Store (MWC1, Index (C1TM, Zero)) }, Store (C6LT,
 *        Index (C6TM, 2)), Store (C7LT, Index (C7TM, 2)), then Return (Package () { 3, C1TM, C6TM, C7TM }): names,
 *        which are read where they point.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If ((PDCB & 0x0200)) __EXPECT__(6084)
    {
        C1TM [Zero] = MWC1 __EXPECT__(6084)
    }

    C6TM [0x02] = C6LT __EXPECT__(6084)
    C7TM [0x02] = C7LT __EXPECT__(6084)
    Return (Package () { 0x03, C1TM, C6TM, C7TM }) __EXPECT__(6084)
}
