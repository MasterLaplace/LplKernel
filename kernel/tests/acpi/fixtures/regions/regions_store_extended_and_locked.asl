// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (DX__, RX__) and Store (Zero, LW__).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    RX = DX __EXPECT__(6084)
    LW = Zero __EXPECT__(6084)
}
