// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The `\_OSI` answers: methods WINS and OTHR set one bit per string `\_OSI` answers Ones for, the
 *        23 Windows strings and 20 others (Linux, Darwin, FreeBSD, HP-UX, OpenVMS, a vendor Linux
 *        string, the feature strings, the empty string, Windows strings that do not exist, a wrong
 *        case, a trailing space, a prefix); NEWS asks for "Windows 2001", "Windows 2015", "Windows
 *        2009", then "Linux"; BADA passes an Integer; the table's code stores `\_OSI ("Windows 2015")`
 *        in TRU_.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (TRU, 0x00)
    Method (WCHK, 2, NotSerialized)
    {
        If ((_OSI (Arg0) == Ones))
        {
            Return ((One << Arg1))
        }

        Return (Zero)
    }

    Method (OCHK, 2, NotSerialized)
    {
        If ((_OSI (Arg0) != Zero))
        {
            Return ((One << Arg1))
        }

        Return (Zero)
    }

    Method (WINS, 0, NotSerialized)
    {
        Local0 = WCHK ("Windows 2000", 0x00)
        Local0 |= WCHK ("Windows 2001", 0x01)
        Local0 |= WCHK ("Windows 2001 SP1", 0x02)
        Local0 |= WCHK ("Windows 2001.1", 0x03)
        Local0 |= WCHK ("Windows 2001 SP2", 0x04)
        Local0 |= WCHK ("Windows 2001.1 SP1", 0x05)
        Local0 |= WCHK ("Windows 2006", 0x06)
        Local0 |= WCHK ("Windows 2006.1", 0x07)
        Local0 |= WCHK ("Windows 2006 SP1", 0x08)
        Local0 |= WCHK ("Windows 2006 SP2", 0x09)
        Local0 |= WCHK ("Windows 2009", 0x0A)
        Local0 |= WCHK ("Windows 2012", 0x0B)
        Local0 |= WCHK ("Windows 2013", 0x0C)
        Local0 |= WCHK ("Windows 2015", 0x0D)
        Local0 |= WCHK ("Windows 2016", 0x0E)
        Local0 |= WCHK ("Windows 2017", 0x0F)
        Local0 |= WCHK ("Windows 2017.2", 0x10)
        Local0 |= WCHK ("Windows 2018", 0x11)
        Local0 |= WCHK ("Windows 2018.2", 0x12)
        Local0 |= WCHK ("Windows 2019", 0x13)
        Local0 |= WCHK ("Windows 2020", 0x14)
        Local0 |= WCHK ("Windows 2021", 0x15)
        Local0 |= WCHK ("Windows 2022", 0x16)
        Return (Local0)
    }

    Method (OTHR, 0, NotSerialized)
    {
        Local0 = OCHK ("Linux", 0x00)
        Local0 |= OCHK ("Darwin", 0x01)
        Local0 |= OCHK ("FreeBSD", 0x02)
        Local0 |= OCHK ("HP-UX", 0x03)
        Local0 |= OCHK ("OpenVMS", 0x04)
        Local0 |= OCHK ("Linux-Dell-Video", 0x05)
        Local0 |= OCHK ("Module Device", 0x06)
        Local0 |= OCHK ("Processor Device", 0x07)
        Local0 |= OCHK ("3.0 Thermal Model", 0x08)
        Local0 |= OCHK ("3.0 _SCP Extensions", 0x09)
        Local0 |= OCHK ("Processor Aggregator Device", 0x0A)
        Local0 |= OCHK ("Extended Address Space Descriptor", 0x0B)
        Local0 |= OCHK ("DisplayMux", 0x0C)
        Local0 |= OCHK ("", 0x0D)
        Local0 |= OCHK ("Windows 2014", 0x0E)
        Local0 |= OCHK ("Windows 99999", 0x0F)
        Local0 |= OCHK ("windows 2015", 0x10)
        Local0 |= OCHK ("Windows 2015 ", 0x11)
        Local0 |= OCHK ("Windows 201", 0x12)
        Local0 |= OCHK ("Windows 2022 SP1", 0x13)
        Return (Local0)
    }

    Method (NEWS, 0, NotSerialized)
    {
        Local0 = _OSI ("Windows 2001")
        Local0 = _OSI ("Windows 2015")
        Local0 = _OSI ("Windows 2009")
        Local0 = _OSI ("Linux") __EXPECT__(3144)
    }

    Method (BADA, 0, NotSerialized)
    {
        Return (_OSI (0x1234))
    }

    TRU = _OSI ("Windows 2015")
}
