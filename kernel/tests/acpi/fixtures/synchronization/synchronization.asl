// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Mutexes MTX0 (SyncLevel 0), MTA1 and MTB1 (1), MTX2 (2), MTX3 (3), M000 to M032 (0), an Event
 *        EVT0 and an Integer INT0, and methods that acquire and release them: NEST nests MTX0, KEEP
 *        keeps MTA1, LOWR acquires MTA1 under MTX2, UNHD releases MTX3 unheld, SAME and DIFF release out
 *        of order at the same level and at another, SAME then acquiring MTX0, NOTM acquires INT0, VIAR acquires MTX0
 * through a reference in Local0, SIGN signals EVT0 twice, SIGI signals INT0, SERL calls the Serialized SER0 (level 0)
 * while holding MTA1, SR3_ calls SER3 (Serialized, level 3), which calls itself twice then acquires MTX2, AFTR acquires
 * MTX2 once SER3 has returned, EWAI waits on EVT0, and MANY acquires the 33 M0xx. CONS signals EVT0 twice, then
 * WAI0, WAI1 and WAI2 take three Waits on it, the second's timeout given by TMO_, which counts its call in CNT0; RSTE
 * signals EVT0 twice, resets it, and returns a Wait on it; RSTI resets INT0, and WAII waits on INT0.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Mutex (MTX0, 0x00)
    Mutex (MTA1, 0x01)
    Mutex (MTB1, 0x01)
    Mutex (MTX2, 0x02)
    Mutex (MTX3, 0x03)
    Event (EVT0)
    Name (INT0, 0x05)
    Method (NEST, 0, NotSerialized)
    {
        Local0 = Acquire (MTX0, 0xFFFF)
        Local1 = Acquire (MTX0, 0x0000)
        Release (MTX0)
        Release (MTX0)
        Return ((Local0 | Local1))
    }

    Method (KEEP, 0, NotSerialized)
    {
        Return (Acquire (MTA1, 0xFFFF))
    }

    Method (LOWR, 0, NotSerialized)
    {
        Acquire (MTX2, 0xFFFF)
        Local0 = Acquire (MTA1, 0xFFFF)
        Release (MTA1)
        Release (MTX2)
        Return (Local0)
    }

    Method (UNHD, 0, NotSerialized)
    {
        Release (MTX3)
    }

    Method (SAME, 0, NotSerialized)
    {
        Acquire (MTA1, 0xFFFF)
        Acquire (MTB1, 0xFFFF)
        Release (MTA1)
        Release (MTB1)
        Local0 = Acquire (MTX0, 0xFFFF)
        Release (MTX0)
        Return (Local0)
    }

    Method (DIFF, 0, NotSerialized)
    {
        Acquire (MTA1, 0xFFFF)
        Acquire (MTX2, 0xFFFF)
        Release (MTA1)
    }

    Method (NOTM, 0, NotSerialized)
    {
        Return (Acquire (INT0, 0xFFFF)) __EXPECT__(6058)
    }

    Method (VIAR, 0, NotSerialized)
    {
        Local0 = RefOf (MTX0)
        Local1 = Acquire (Local0, 0xFFFF)
        Release (Local0)
        Return (Local1)
    }

    Method (SIGN, 0, NotSerialized)
    {
        Signal (EVT0)
        Signal (EVT0)
    }

    Method (SIGI, 0, NotSerialized)
    {
        Signal (INT0) __EXPECT__(6058)
    }

    Method (SER0, 0, Serialized)
    {
        Return (One)
    }

    Method (SERL, 0, NotSerialized)
    {
        Acquire (MTA1, 0xFFFF)
        Return (SER0 ())
    }

    Method (SER3, 1, Serialized, 3)
    {
        If (Arg0)
        {
            Return (SER3 ((Arg0 - 0x01)))
        }

        Return (Acquire (MTX2, 0xFFFF))
    }

    Method (SR3, 0, NotSerialized)
    {
        Return (SER3 (0x02))
    }

    Method (AFTR, 0, NotSerialized)
    {
        SER3 (0x00)
        Local0 = Acquire (MTX2, 0xFFFF)
        Release (MTX2)
        Return (Local0)
    }

    Method (EWAI, 0, NotSerialized)
    {
        Return (Wait (EVT0, 0x00))
    }

    Name (CNT0, Zero)
    Name (WAI0, 0x05)
    Name (WAI1, 0x05)
    Name (WAI2, 0x05)
    Method (TMO, 0, NotSerialized)
    {
        CNT0++
        Return (0xFFFF)
    }

    Method (CONS, 0, NotSerialized)
    {
        Signal (EVT0)
        Signal (EVT0)
        WAI0 = Wait (EVT0, Zero)
        WAI1 = Wait (EVT0, TMO ())
        WAI2 = Wait (EVT0, 0xFFFF)
    }

    Method (RSTE, 0, NotSerialized)
    {
        Signal (EVT0)
        Signal (EVT0)
        Reset (EVT0)
        Return (Wait (EVT0, Zero))
    }

    Method (RSTI, 0, NotSerialized)
    {
        Reset (INT0) __EXPECT__(6058)
    }

    Method (WAII, 0, NotSerialized)
    {
        Return (Wait (INT0, Zero)) __EXPECT__(6058)
    }

    Mutex (M000, 0x00)
    Mutex (M001, 0x00)
    Mutex (M002, 0x00)
    Mutex (M003, 0x00)
    Mutex (M004, 0x00)
    Mutex (M005, 0x00)
    Mutex (M006, 0x00)
    Mutex (M007, 0x00)
    Mutex (M008, 0x00)
    Mutex (M009, 0x00)
    Mutex (M010, 0x00)
    Mutex (M011, 0x00)
    Mutex (M012, 0x00)
    Mutex (M013, 0x00)
    Mutex (M014, 0x00)
    Mutex (M015, 0x00)
    Mutex (M016, 0x00)
    Mutex (M017, 0x00)
    Mutex (M018, 0x00)
    Mutex (M019, 0x00)
    Mutex (M020, 0x00)
    Mutex (M021, 0x00)
    Mutex (M022, 0x00)
    Mutex (M023, 0x00)
    Mutex (M024, 0x00)
    Mutex (M025, 0x00)
    Mutex (M026, 0x00)
    Mutex (M027, 0x00)
    Mutex (M028, 0x00)
    Mutex (M029, 0x00)
    Mutex (M030, 0x00)
    Mutex (M031, 0x00)
    Mutex (M032, 0x00)
    Method (MANY, 0, NotSerialized)
    {
        Acquire (M000, 0xFFFF)
        Acquire (M001, 0xFFFF)
        Acquire (M002, 0xFFFF)
        Acquire (M003, 0xFFFF)
        Acquire (M004, 0xFFFF)
        Acquire (M005, 0xFFFF)
        Acquire (M006, 0xFFFF)
        Acquire (M007, 0xFFFF)
        Acquire (M008, 0xFFFF)
        Acquire (M009, 0xFFFF)
        Acquire (M010, 0xFFFF)
        Acquire (M011, 0xFFFF)
        Acquire (M012, 0xFFFF)
        Acquire (M013, 0xFFFF)
        Acquire (M014, 0xFFFF)
        Acquire (M015, 0xFFFF)
        Acquire (M016, 0xFFFF)
        Acquire (M017, 0xFFFF)
        Acquire (M018, 0xFFFF)
        Acquire (M019, 0xFFFF)
        Acquire (M020, 0xFFFF)
        Acquire (M021, 0xFFFF)
        Acquire (M022, 0xFFFF)
        Acquire (M023, 0xFFFF)
        Acquire (M024, 0xFFFF)
        Acquire (M025, 0xFFFF)
        Acquire (M026, 0xFFFF)
        Acquire (M027, 0xFFFF)
        Acquire (M028, 0xFFFF)
        Acquire (M029, 0xFFFF)
        Acquire (M030, 0xFFFF)
        Acquire (M031, 0xFFFF)
        Acquire (M032, 0xFFFF)
    }
}
