// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief PKG_, a Package of one element, then RGF_, whose offset is DerefOf (Index (PKG_, 5)), past that
 *        element.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (PKG, Package (0x01)
    {
        Zero
    })
    OperationRegion (RGF, SystemIO, DerefOf (PKG [0x05]), (One + One))
}
