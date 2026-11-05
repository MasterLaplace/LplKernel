// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (MWC1, Index (C1TM, Zero)): Intel's pattern puts the MWAIT register in C1's package.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    C1TM [Zero] = MWC1 __EXPECT__(6084)
}
