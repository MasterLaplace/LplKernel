// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Every comparison and operator a record runs, as MAIN: 20 turns of `Local0++`, then Ifs on
 *        `Local0 > 15`, `== 7`, `!= 9`, `<= 3`, `>= 18`, `!(Local0 & 1)`, `(Local0 << 62) > 0`,
 *        `(Local0 << 64) == 0` and `((Local0 * 3) - 1) % 5 == 0`, each adding its own power of ten to
 *        Local1, which MAIN returns; at the fifth turn only, an If whose predicate `1 + (2 + (3 + (4 +
 *        Local0))) == 15` holds 6 values, more than the loop held before, first run from its record.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MAIN, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x14))
        {
            Local0++
            If ((Local0 > 0x0F))
            {
                Local1++
            }

            If ((Local0 == 0x07))
            {
                Local1 += 0x64
            }

            If ((Local0 != 0x09))
            {
                Local1 += 0x03E8
            }

            If ((Local0 <= 0x03))
            {
                Local1 += 0x2710
            }

            If ((Local0 >= 0x12))
            {
                Local1 += 0x000186A0
            }

            If (!(Local0 & One))
            {
                Local1 += 0x000F4240
            }

            If (((Local0 << 0x3E) > Zero))
            {
                Local1 += 0x00989680
            }

            If (((Local0 << 0x40) == Zero))
            {
                Local1 += 0x05F5E100
            }

            If (((((Local0 * 0x03) - One) % 0x05) ==
                Zero))
            {
                Local1 += 0x3B9ACA00
            }

            If ((Local0 == 0x05))
            {
                If (((0x01 + (0x02 + (0x03 + (0x04 + Local0)
                    ))) == 0x0F))
                {
                    Local1 += 0x0A
                }
            }
        }

        Return (Local1)
    }
}
