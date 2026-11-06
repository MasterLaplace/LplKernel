// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A statement at offset 0 that reads names: `If (LEqual (And (STS_, 0x10), FLAG)) {}`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If (((STS & 0x10) == FLAG)){} __EXPECT__(6084)
}
