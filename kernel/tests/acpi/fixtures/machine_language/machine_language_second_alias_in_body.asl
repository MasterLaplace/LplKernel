// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MTH1 of one argument and MTH2 of two; MAIN declares Alias (MTH1, ALI_) then Alias (MTH2, ALI_), and calls
 *        ALI_ (One).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MTH1, 1, NotSerialized)
    {
        Noop
    }
    Method (MTH2, 2, NotSerialized)
    {
        Noop
    }
    Method (MAIN, 0, NotSerialized)
    {
        Alias (MTH1, ALI_)
        Alias (MTH2, ALI_) __EXPECT__(6074)
        ALI_ (One)
        Noop
    }
}
