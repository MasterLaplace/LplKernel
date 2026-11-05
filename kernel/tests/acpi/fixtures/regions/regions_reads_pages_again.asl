// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (P01_, Local0) then Store (P00_, Local0): page 1 again, then page 0, which 64 later pages evicted.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (P01_, Local0) __EXPECT__(6084)
    Store (P00_, Local0) __EXPECT__(6084)
}
