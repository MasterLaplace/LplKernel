// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A ThermalZone, a Device, a Method, a Processor and a PowerResource each declared twice, the second
 *        with a body of its own, then a Scope over the Device.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (MAIN, 0x00)
    ThermalZone (TEST)
    {
    }
    ThermalZone (TEST) __EXPECT__(6074)
    {
        Name (INNR, 0x01)
        Store (0x55, MAIN)
    }
    Device (DEV0) __EXPECT__(3141)
    {
        Name (AAAA, 0x01)
    }
    Device (DEV0) __EXPECT__(3141) __EXPECT__(6074)
    {
        Name (BBBB, 0x02)
        Noop
    }
    Method (MTH0, 0, NotSerialized)
    {
        Return (One)
    }
    Method (MTH0, 0, NotSerialized) __EXPECT__(6074)
    {
        Stall (0x01)
        Return (0x02)
    }
    Processor (CPU0, 0x01, 0x00000410, 0x06) __EXPECT__(3168)
    {
        Name (PRA_, 0x01)
    }
    Processor (CPU0, 0x02, 0x00000420, 0x06) __EXPECT__(3168) __EXPECT__(6074)
    {
        Name (PRB_, 0x02)
    }
    PowerResource (PWR0, 0x00, 0x0000)
    {
        Name (PWA_, 0x01)
    }
    PowerResource (PWR0, 0x01, 0x0001) __EXPECT__(6074)
    {
        Name (PWB_, 0x02)
    }
    Scope (DEV0)
    {
        Name (CCCC, 0x03)
    }
}
