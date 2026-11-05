// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A method that counts Local0 up to 5, acquires \MUTX, Loads TBLR into \HND1, counts on to 8, then
 *        releases \MUTX.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Local0 = Zero
    While ((Local0 < 0x05))
    {
        Local0++
    }

    Acquire (\MUTX, 0xFFFF) __EXPECT__(6084)
    Load (\TBLR, \HND1) __EXPECT__(6084)
    While ((Local0 < 0x08))
    {
        Local0++
    }

    Release (\MUTX) __EXPECT__(6084)
}
