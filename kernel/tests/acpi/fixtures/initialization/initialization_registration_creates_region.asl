// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of a _REG (2) that traces Arg0 then Arg1, then creates a PCI_Config region ^DYNZ in the scope
 *        above its own.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (Multiply (Add (Multiply (\TRAC, 0x10), Arg0), 0x10), Arg1), \TRAC) __EXPECT__(6084)
    OperationRegion (^DYNZ, PCI_Config, Zero, 0x10) __EXPECT__(6160)
}
