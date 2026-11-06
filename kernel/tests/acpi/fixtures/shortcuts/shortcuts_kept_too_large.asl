// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A package of three Buffers of 4 KiB, each a run of two pages of its own: `Name (FL__, Package (3)
 *        {Buffer (0x1000) {}, Buffer (0x1000) {}, Buffer (0x1000) {}})`, whose third no longer fits 16 KiB.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (FL, Package (0x03)
    {
        Buffer (0x1000){},
        Buffer (0x1000){},
        Buffer (0x1000){}
    })
}
