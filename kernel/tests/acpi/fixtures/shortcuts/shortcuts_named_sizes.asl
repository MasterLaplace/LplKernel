// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Sizes a name gives: `Name (CNT_, 4)`, `Name (B3__, Buffer (CNT_) {})`,
 *        `Name (P3__, Package (2) {Package (3) {1}, Buffer (CNT_) {}})`, which pads one package and builds the
 *        other, `Method (MCNT) { Return (4) }` and `Name (B4__, Buffer (MCNT) {})`, which waits for the
 *        interpreter; then table-level code that stores 8 to CNT_.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (CNT, 0x04)
    Name (B3, Buffer (CNT){})
    Name (P3, Package (0x02)
    {
        Package (0x03)
        {
            One
        },

        Buffer (CNT){}
    })
    Method (MCNT, 0, NotSerialized)
    {
        Return (0x04)
    }

    Name (B4, Buffer (MCNT ()){})
    CNT = 0x08
}
