// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MAIN stores \_SB.D299.MTH_ (Zero), the last of the 300 methods the Externals name, to Local0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MAIN, 0, NotSerialized)
    {
        Store (\_SB.D299.MTH (Zero), Local0) __EXPECT__(3144) __EXPECT__(6084)
    }
}
