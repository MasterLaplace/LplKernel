// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief DataTableRegion (DTR0, "DSDT", "", "") at table level, then RES_ takes 7.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES, 0x05)
    DataTableRegion (DTR0, "DSDT", "", "")
    RES = 0x07
}
