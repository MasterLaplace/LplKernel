// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Local0 = Zero While ((Local0 < 0x0100)) { \OREV = Local0 Load (\TBLR, Local1) Local0++ }.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Local0 = Zero
    While ((Local0 < 0x0100))
    {
        \OREV = Local0 __EXPECT__(6084)
        Load (\TBLR, Local1) __EXPECT__(6084)
        Local0++
    }
}
