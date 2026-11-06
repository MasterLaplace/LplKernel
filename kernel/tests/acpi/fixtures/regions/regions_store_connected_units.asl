// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (CU__, RC__) and Store (CB__, RB__): the reads of the units after each Connection.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    RC = CU __EXPECT__(6084)
    RB = CB __EXPECT__(6084)
}
