// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The SSDT's RGC_, a SystemIO region at the same offset of its table, whose offset is 2 + 3.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (RGC, SystemIO, (0x02 + 0x03), 0x04)
}
