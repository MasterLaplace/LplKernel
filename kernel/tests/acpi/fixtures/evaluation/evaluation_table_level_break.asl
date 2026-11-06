// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief BEFR, a Break and a Continue at table level, outside any While, then AFTR.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BEFR, One)
    Break __EXPECT__(6081)
    Continue __EXPECT__(6081)
    Name (AFTR, One)
}
