// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MAIN asks CondRefOf (MTHB, Local0) of a method of one argument, then calls MTHB (Zero) in the If's
 *        body.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MTHB, 1, NotSerialized)
    {
        Return (Arg0)
    }

    Method (MAIN, 0, NotSerialized)
    {
        If (CondRefOf (MTHB, Local0)) __EXPECT__(3144)
        {
            Local1 = MTHB (Zero) __EXPECT__(3144)
        }
    }
}
