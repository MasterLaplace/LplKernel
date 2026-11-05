// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A Device DEVX that holds the Name INNR, then the Name AFTD after it, which the test puts at the bottom of
 *        nested If (One).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Device (DEVX) __EXPECT__(3141)
    {
        Name (INNR, One)
    }

    Name (AFTD, One)
}
