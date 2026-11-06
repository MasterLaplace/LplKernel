// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Name (\NOPE.ABCD, One), whose path does not exist, then a method GOOD.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (\NOPE.ABCD, One) __EXPECT__(6160)
    Method (GOOD, 0, NotSerialized)
    {
        Noop
    }
}
