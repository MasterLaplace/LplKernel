// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A field whose read fails in a loop, as FAIL: BADF, a byte of an EmbeddedControl region, which no
 *        handler serves; `Local0 = 0; Local1 = 0; While (Local0 < 4) { Local0++; If (Local0 == 2) { Local1 =
 *        (((Local0 + 1) + Local0) + BADF) } }; Return (Local1)`: the loop holds 3 values for that statement before
 *        it reads BADF, more than anything before, and fails there with space_unsupported at the second turn.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (ECRG, EmbeddedControl, Zero, 0x10)
    Field (ECRG, ByteAcc, NoLock, Preserve)
    {
        BADF,   8
    }

    Method (FAIL, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x04))
        {
            Local0++
            If ((Local0 == 0x02))
            {
                Local1 = (((Local0 + One) + Local0) + BADF)
            }
        }

        Return (Local1)
    }
}
