// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A statement at offset 0 that reads a path: `Local1 = \_SB.PCI0.ECRD`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Local1 = \_SB.PCI0.ECRD __EXPECT__(6084)
}
