// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MWID stores Arg0 to Local1 and returns it, and stores LEqual (Arg0, One) to RES1.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES1, Zero)
    Method (MWID, 1, NotSerialized)
    {
        Local1 = Arg0
        RES1 = (Arg0 == One)
        Return (Local1)
    }
}
