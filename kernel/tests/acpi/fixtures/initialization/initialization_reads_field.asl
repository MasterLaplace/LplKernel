// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of READ: Local0 takes FLD0, a loop turns once, and READ returns Local0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Local0 = FLD0 __EXPECT__(6084)
    Local1 = Zero
    While ((Local1 < One))
    {
        Local1++
    }

    Return (Local0)
}
