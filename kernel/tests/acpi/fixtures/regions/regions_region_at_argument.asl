// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief OperationRegion (MR__, SystemMemory, Arg0, 2): the region MREG creates at the offset its argument gives.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (MR, SystemMemory, Arg0, 0x02)
}
