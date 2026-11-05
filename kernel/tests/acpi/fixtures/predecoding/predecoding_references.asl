// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Operands that hold a reference, read only by records, as CALL: `Return (MAIN (RefOf (INT0)))`, MAIN (Arg0)
 *        setting Local2 to RefOf (INT0) and Local3 to Index (PKG0, 0), then running `While (Local0 < 4) { Local0++;
 *        If (Local0 == 2) { If (Local2 > Local0) { Local1++ }; If (Local3 > Local0) { Local1++ }; If (Arg0 >
 *        Local0) { Local1++ } } }`: the three Ifs are reached at the second turn, the first from records.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (INT0, 0x09)
    Name (PKG0, Package (0x01)
    {
        0x09
    })
    Method (MAIN, 1, NotSerialized)
    {
        Store (Zero, Local0)
        Store (Zero, Local1)
        Store (RefOf (INT0), Local2)
        Store (Index (PKG0, Zero), Local3)
        While (LLess (Local0, 0x04))
        {
            Increment (Local0)
            If (LEqual (Local0, 0x02))
            {
                If (LGreater (Local2, Local0))
                {
                    Increment (Local1)
                }

                If (LGreater (Local3, Local0))
                {
                    Increment (Local1)
                }

                If (LGreater (Arg0, Local0))
                {
                    Increment (Local1)
                }
            }
        }

        Return (Local1)
    }

    Method (CALL, 0, NotSerialized)
    {
        Return (MAIN (RefOf (INT0)))
    }
}
