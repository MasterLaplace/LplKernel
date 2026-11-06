// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief FOO_, a method of one argument at the root; MAIN declares a Name FOO_ that hides it, and a Name \TMPN
 *        at the root; AFTR calls FOO_ (Zero) and reads TMPN.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (FOO, 1, NotSerialized)
    {
        Return (Arg0)
    }

    Method (MAIN, 0, NotSerialized)
    {
        Name (FOO, 0x05)
        Local0 = FOO __EXPECT__(3144)
        Name (\TMPN, One)
    }

    Method (AFTR, 0, NotSerialized)
    {
        Local1 = FOO (Zero) __EXPECT__(3144)
        Local2 = TMPN __EXPECT__(3144)
    }
}
