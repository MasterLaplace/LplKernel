// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (MIDX, RI__) and Store (MBNK, RB__): the table's calls of the two methods.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    RI = MIDX __EXPECT__(6084)
    RB = MBNK __EXPECT__(6084)
}
