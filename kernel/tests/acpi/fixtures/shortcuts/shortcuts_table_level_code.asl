// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Statements inside table-level code: `Name (N0__, One)`, `If (N0__) {Device (D0__) {Name (N1__, 1)}
 *        Method (M0__) {}} Else {Name (N2__, 2)}` and `Store (2, N0__)`: through the decoder, the 4 declarations
 *        of the code the load runs, the Else stepped over, and the 5 of both branches where it declares every
 *        branch.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (N0, One)
    If (N0)
    {
        Device (D0) __EXPECT__(3141)
        {
            Name (N1, One)
        }

        Method (M0, 0, NotSerialized)
        {
        }
    }
    Else
    {
        Name (N2, 0x02)
    }

    N0 = 0x02
}
