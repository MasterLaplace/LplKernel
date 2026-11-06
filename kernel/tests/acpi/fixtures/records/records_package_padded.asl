// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief PKG_ a Package of two elements given one initialiser, the Integer 4: its second element is Uninitialized.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (PKG_, Package (2) { 0x04 })
}
