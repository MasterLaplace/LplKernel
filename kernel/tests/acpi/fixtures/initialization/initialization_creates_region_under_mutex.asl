// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The creator's statements: Acquire (\MTXA, 0xFFFF), a PCI_Config region ^DYNR in the scope above, then
 *        Release (\MTXA).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Acquire (\MTXA, 0xFFFF) __EXPECT__(6084)
    OperationRegion (^DYNR, PCI_Config, Zero, 0x10) __EXPECT__(6160)
    Release (\MTXA) __EXPECT__(6084)
}
