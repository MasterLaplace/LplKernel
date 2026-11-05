// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of MTYP: OperationRegion (MT__, SystemMemory, 0x10000, 4), then Return (ObjectType (MT__)).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (MT, SystemMemory, 0x00010000, 0x04)
    Return (ObjectType (MT))
}
