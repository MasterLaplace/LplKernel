// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief OperationRegion (SKPR, SystemMemory, Add (BASE, Zero), One): the region of a branch the load does not take.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (SKPR, SystemMemory, (BASE + Zero), One) __EXPECT__(6084)
}
