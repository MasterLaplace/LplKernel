// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Table-level code: If (Zero) declares AAAA, its Else BBBB; If (One) declares CCCC, its Else DDDD; then RES_
 *        takes 0x2A through Local0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If (Zero)
    {
        Name (AAAA, One)
    }
    Else
    {
        Name (BBBB, 0x02)
    }

    If (One)
    {
        Name (CCCC, 0x03)
    }
    Else
    {
        Name (DDDD, 0x04)
    }

    Name (RES, Zero)
    Local0 = 0x2A
    RES = Local0
}
