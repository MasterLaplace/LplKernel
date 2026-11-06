// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief In DEV0, MTH0 stores Revision, which §5.2 leaves unimplemented, and MTH1 divides One by Zero.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Device (DEV0) __EXPECT__(3141)
    {
        Method (MTH0, 0, NotSerialized)
        {
            Store (Revision, Local0) __EXPECT__(3144)
        }

        Method (MTH1, 0, NotSerialized)
        {
            Store (Divide (One, Zero, Local1, Local2), Local0) __EXPECT__(3144)
        }
    }
}
