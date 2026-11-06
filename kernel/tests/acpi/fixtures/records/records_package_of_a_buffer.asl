// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief PKG_ a Package of a Buffer of four zeros, whose bytes, without the element's type, fold as those of a
 *        Package of the Integer 4.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (PKG_, Package (1) { Buffer (4) { 0x00, 0x00, 0x00, 0x00 } })
}
