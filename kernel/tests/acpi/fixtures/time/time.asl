// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Methods that sleep 60 s per turn of an endless loop (S60K), 3 s (S3K_), 3 s Arg0 times (SLPN), 70 ms until
 * refused (S70_), and stall Arg0 µs (STAL); TICK reads Timer twice, TSLP around a Sleep (5), POLL loops until Timer
 * passes 10 ms ahead, TIMR reads it once, NONE sleeps and stalls 0. TURN counts the turns of the loops.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (TURN, 0x00)
    Method (S60K, 0, NotSerialized)
    {
        While (One)
        {
            Sleep (60000)
        }
    }

    Method (S3K, 0, NotSerialized)
    {
        Sleep (3000)
    }

    Method (SLPN, 1, NotSerialized)
    {
        TURN = 0x00
        While ((TURN < Arg0))
        {
            Sleep (3000)
            TURN++
        }
    }

    Method (S70, 0, NotSerialized)
    {
        TURN = 0x00
        While (One)
        {
            Sleep (70)
            TURN++
        }
    }

    Method (STAL, 1, NotSerialized)
    {
        Stall (Arg0)
    }

    Method (TICK, 0, NotSerialized)
    {
        Local0 = Timer
        Local1 = Timer
        Return ((Local1 - Local0))
    }

    Method (TSLP, 0, NotSerialized)
    {
        Local0 = Timer
        Sleep (0x05)
        Return ((Timer - Local0))
    }

    Method (POLL, 0, NotSerialized)
    {
        Local1 = 0x00
        Local0 = (Timer + 100000)
        While ((Timer <= Local0))
        {
            Local1++
        }

        Return (Local1)
    }

    Method (TIMR, 0, NotSerialized)
    {
        Return (Timer)
    }

    Method (NONE, 0, NotSerialized)
    {
        Sleep (0x00)
        Stall (0x00)
    }
}
