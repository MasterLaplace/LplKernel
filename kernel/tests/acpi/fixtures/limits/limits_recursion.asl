// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M007 calls itself Arg0 times, then returns 0; the table's code stores M007 (62) to RES0 and M007 (63) to
 *        RES1, both 5 before.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES0, 0x05)
    Name (RES1, 0x05)
    Method (M007, 1, NotSerialized)
    {
        If (Arg0)
        {
            Return (M007 ((Arg0 - One)))
        }

        Return (Zero)
    }

    RES0 = M007 (0x3E)
    RES1 = M007 (0x3F)
}
