// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Load (\CSTR, \HC0) If ((\OSCP & 0x40)) { Load (\HWPR, \HW0) }.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Load (\CSTR, \HC0) __EXPECT__(6084)
    If ((\OSCP & 0x40)) __EXPECT__(6084)
    {
        Load (\HWPR, \HW0) __EXPECT__(6084)
    }
}
