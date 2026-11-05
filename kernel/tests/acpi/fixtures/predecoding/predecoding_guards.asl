// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Operands a record hands back, as MAIN: a While whose bound is the String "4", Ifs on an
 *        uninitialized Local5, on `DerefOf (Local2)` with Local2 = RefOf (INT0), on a Buffer, and on a
 *        String; and as MODZ, a modulo by a local that reaches 0 on the third turn, which fails the
 *        evaluation.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (INT0, 0x05)
    Method (MAIN, 0, NotSerialized)
    {
        Local1 = Zero
        Local2 = RefOf (INT0)
        Local3 = "4"
        Local4 = Buffer (One)
            {
                 0x02
            }
        While ((Local1 < Local3))
        {
            Local1++
            If ((Local5 == Zero)) __EXPECT__(6066)
            {
                INT0++
            }

            If ((DerefOf (Local2) > 0x05))
            {
                INT0 += 0x0A
            }

            If ((Local4 > One))
            {
                INT0 += 0x64
            }

            If ((Local3 == "4"))
            {
                INT0 += 0x03E8
            }
        }

        Return ((INT0 + (Local1 * 0x00010000)))
    }

    Method (MODZ, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = 0x03
        While ((Local0 < 0x03))
        {
            Local0++
            Local1--
            If (((0x0A % Local1) == 0x07))
            {
                Local0 += 0x10
            }
        }

        Return (Local0)
    }
}
