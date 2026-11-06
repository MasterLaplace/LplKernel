// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Name (PKG0, Package (4) {7, NAM1, MTH1}): elements that are an Integer, names and an Uninitialized one.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (PKG0, Package (0x04)
    {
        0x07,
        NAM1, __EXPECT__(6084)
        MTH1 __EXPECT__(6084)
    })
}
