// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MSTO stores 7 over Buffer (2) {1, 2} in Local0 and 9 through RefOf (INT0) in Local1, then reads both into
 *        RES0 and RES1; MARG stores 7 over Buffer (2) {3, 4} in Arg0 and returns it.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (INT0, 0x05)
    Name (RES0, Zero)
    Name (RES1, Zero)
    Method (MSTO, 0, NotSerialized)
    {
        Local0 = Buffer (0x02)
            {
                 0x01, 0x02
            }
        Local0 = 0x07
        Local1 = RefOf (INT0)
        Local1 = 0x09
        RES0 = Local0
        RES1 = DerefOf (Local1)
    }

    Method (MARG, 1, NotSerialized)
    {
        Arg0 = Buffer (0x02)
            {
                 0x03, 0x04
            }
        Arg0 = 0x07
        Return (Arg0)
    }
}
