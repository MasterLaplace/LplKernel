// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The code of the table the tests load at the caps of §6.1: two Stores of \NOPE, a name no table declares,
 *        each followed by a declaration its failure hands the load, OperationRegion (RGNA) of a computed offset, then
 *        Device (DEVB), which holds INDV; then Name (AFTR, 7).
 */
DefinitionBlock ("", "SSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \TRAC = \NOPE __EXPECT__(6084)
    OperationRegion (RGNA, SystemMemory, (One + One), 0x04)
    \TRAC = \NOPE __EXPECT__(6084)
    Device (DEVB) __EXPECT__(3141)
    {
        Name (INDV, One)
    }

    Name (AFTR, 0x07)
}
