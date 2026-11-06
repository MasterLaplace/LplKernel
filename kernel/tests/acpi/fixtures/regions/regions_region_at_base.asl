// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief OperationRegion (NAMR, SystemMemory, Add (BASE, Zero), One): a region whose offset an expression gives.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (NAMR, SystemMemory, (BASE + Zero), One) __EXPECT__(6084)
}
