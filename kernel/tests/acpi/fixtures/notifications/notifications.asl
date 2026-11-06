// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A device DEV0, a processor CPU0, a thermal zone TZ00, a power resource PWR0 and an Integer
 *        INT0; the table's code notifies DEV0 with 2; NTF3 notifies DEV0, CPU0 and TZ00 with 0x80 to
 *        0x82, NTFI notifies INT0, NTFP notifies PWR0, NTFL notifies DEV0 with 0x90 through a reference
 *        held in Local0, and NTFM notifies DEV0 65 times, with 0 to 64.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Device (DEV0) __EXPECT__(3141)
    {
    }

    Processor (CPU0, 0x01, 0x00000810, 0x06){} __EXPECT__(3168)
    ThermalZone (TZ00)
    {
    }

    PowerResource (PWR0, 0x00, 0x0000){}
    Name (INT0, 0x01)
    Notify (DEV0, 0x02)
    Method (NTF3, 0, NotSerialized)
    {
        Notify (DEV0, 0x80)
        Notify (CPU0, 0x81)
        Notify (TZ00, 0x82)
    }

    Method (NTFI, 0, NotSerialized)
    {
        Notify (INT0, 0x80) __EXPECT__(6058)
    }

    Method (NTFP, 0, NotSerialized)
    {
        Notify (PWR0, 0x80) __EXPECT__(6058)
    }

    Method (NTFL, 0, NotSerialized)
    {
        Local0 = RefOf (DEV0)
        Notify (Local0, 0x90)
    }

    Method (NTFM, 0, NotSerialized)
    {
        Local0 = 0x00
        While ((Local0 < 0x41))
        {
            Notify (DEV0, Local0)
            Local0++
        }
    }
}
