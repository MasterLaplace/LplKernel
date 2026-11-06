// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Stores to Debug a Buffer longer than a line shows, a Package, a reference to PKG0 and an empty String.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (PKG0, Package (0x01)
    {
        One
    })
    Debug = Buffer (0x14) {}
    Debug = Package (0x02)
        {
            One,
            0x02
        }
    Debug = RefOf (PKG0)
    Debug = ""
}
