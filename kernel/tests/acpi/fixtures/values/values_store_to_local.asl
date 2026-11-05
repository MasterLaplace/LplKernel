// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MCPY stores BUF0 to Local0, then 0xFF to BUF0, and returns Local0; MARG stores "ZZ" to its argument; the
 *        table's code copies their results to RES0 and RES1, MARG given BUF0, then "STR" to INT0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BUF0, Buffer (0x03)
    {
         0x01, 0x02, 0x03
    })
    Name (RES0, 0x00)
    Name (RES1, 0x00)
    Name (INT0, 0x05)
    Method (MCPY, 0, NotSerialized)
    {
        Local0 = BUF0
        BUF0 = 0xFF
        Return (Local0)
    }

    Method (MARG, 1, NotSerialized)
    {
        Arg0 = "ZZ"
        Return (Arg0)
    }

    CopyObject (MCPY (), RES0)
    CopyObject (MARG (BUF0), RES1)
    CopyObject ("STR", INT0)
}
