// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MAIN calls MTHA (One, Zero), MTHA (Zero, One) and MTH4 (Zero, Zero, Zero, Zero), each declared after
 *        MAIN with the arguments it takes.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MAIN, 0, NotSerialized)
    {
        Local0 = MTHA (One, Zero) __EXPECT__(3144)
        Local1 = MTHA (Zero, One) __EXPECT__(3144)
        Local2 = MTH4 (Zero, Zero, Zero, Zero) __EXPECT__(3144)
    }

    Method (MTHA, 2, NotSerialized)
    {
        Return (Arg0)
    }

    Method (MTH4, 4, NotSerialized)
    {
        Return (Arg3)
    }
}
