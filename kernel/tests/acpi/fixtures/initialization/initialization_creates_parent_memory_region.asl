// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of MKMM: a SystemMemory region ^DYNM in the scope above the method's.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (^DYNM, SystemMemory, 0x3000, 0x10) __EXPECT__(6160)
}
