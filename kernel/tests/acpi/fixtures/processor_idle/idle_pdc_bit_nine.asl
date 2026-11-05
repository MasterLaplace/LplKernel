// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief And (PDCB, 0x200): the predicate of the If of Intel's pattern, bit 9 of what the _PDC was given.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    (PDCB & 0x0200) __EXPECT__(6084) __EXPECT__(6114)
}
