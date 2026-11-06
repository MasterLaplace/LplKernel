// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Load (\TBLR, Local0) then Return (Zero): the body of a _BBN that loads a table.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Load (\TBLR, Local0) __EXPECT__(6084)
    Return (Zero)
}
