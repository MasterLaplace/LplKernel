// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Table-level code that switches on Local0 twice, as firmware does at the root: Local0 takes 2, then a Switch
 *        whose Case (2) stores 20 to RES0; Local0 takes 5, then a Switch whose Default stores 30 to RES1. iasl makes
 *        each Switch a Name at the root, _T_0 and _T_1, that keeps its value, and a While (One) that ends on a Break.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES0, Zero)
    Name (RES1, Zero)
    Store (0x02, Local0)
    Switch (ToInteger (Local0))
    {
        Case (One)
        {
            Store (0x0A, RES0)
        }
        Case (0x02)
        {
            Store (0x14, RES0)
        }
        Default
        {
            Store (0x1E, RES0)
        }
    }
    Store (0x05, Local0)
    Switch (ToInteger (Local0))
    {
        Case (One)
        {
            Store (0x0A, RES1)
        }
        Case (0x02)
        {
            Store (0x14, RES1)
        }
        Default
        {
            Store (0x1E, RES1)
        }
    }
}
