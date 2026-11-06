// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Control flow inside loops, as MAIN (Arg0): a While (One) whose inner While breaks when Local2
 *        reaches 13, then `Local0++`, a Continue when Local0 is a multiple of 3, else `Local1 += TWCE
 *        (Local0)` (a call), a Return when Local0 is Arg0, a Break at 10.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (TWCE, 1, NotSerialized)
    {
        Return ((Arg0 * 0x02))
    }

    Method (MAIN, 1, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        Local2 = Zero
        While (One)
        {
            Local3 = Zero
            While ((Local3 < 0x05))
            {
                Local3++
                Local2++
                If ((Local2 == 0x0D))
                {
                    Break
                }
            }

            Local0++
            If (((Local0 % 0x03) == Zero))
            {
                Continue
            }
            Else
            {
                Local1 += TWCE (Local0)
            }

            If ((Local0 == Arg0))
            {
                Return ((Local1 + (Local2 * 0x03E8)))
            }

            If ((Local0 >= 0x0A))
            {
                Break
            }
        }

        Return ((Local1 + 0x00100000))
    }
}
