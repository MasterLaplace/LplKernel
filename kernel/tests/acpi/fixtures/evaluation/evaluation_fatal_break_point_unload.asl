// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A BreakPoint, then a Fatal (3, 4, 5), at table level, then RES_ takes 7. BRKP runs a BreakPoint and returns
 *        1, FATL runs Fatal (1, 2, ARGM ()) and returns 2, FATP runs Fatal (1, 2, PKG0), UNLD unloads TBLX and returns
 *        3, and UNLI unloads Index (PKG0, INDM ()) and returns 4; ARGM and INDM each count their call in CNT0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (RES, 0x05)
    Name (TBLX, Zero)
    Name (CNT0, Zero)
    Name (PKG0, Package (0x01)
    {
        Zero
    })
    Method (ARGM, 0, NotSerialized)
    {
        CNT0++
        Return (0x2A)
    }

    Method (INDM, 0, NotSerialized)
    {
        CNT0++
        Return (Zero)
    }

    Method (BRKP, 0, NotSerialized)
    {
        BreakPoint
        Return (One)
    }

    Method (FATL, 0, NotSerialized)
    {
        Fatal (0x01, 0x00000002, ARGM ())
        Return (0x02)
    }

    Method (FATP, 0, NotSerialized)
    {
        Fatal (0x01, 0x00000002, PKG0) __EXPECT__(6058)
        Return (0x05)
    }

    Method (UNLD, 0, NotSerialized)
    {
        Unload (TBLX) __EXPECT__(3157)
        Return (0x03)
    }

    Method (UNLI, 0, NotSerialized)
    {
        Unload (Index (PKG0, INDM ())) __EXPECT__(3157)
        Return (0x04)
    }

    BreakPoint
    Fatal (0x03, 0x00000004, 0x05)
    RES = 0x07
}
