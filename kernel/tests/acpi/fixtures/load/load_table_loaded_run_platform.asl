// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief CreateDWordField (Arg3, 0x04, CDW2) \OSCP = CDW2 Return (Arg3).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    CreateDWordField (Arg3, 0x04, CDW2)
    \OSCP = CDW2 __EXPECT__(6084)
    Return (Arg3)
}
