// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of REC_ (Arg0): it calls itself Arg0 times, then Loads TBLR into \HND1.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If (Arg0)
    {
        REC_ (Subtract (Arg0, One)) __EXPECT__(6084)
    }
    Else
    {
        Load (\TBLR, \HND1) __EXPECT__(6084)
    }
}
