// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief SystemIO fields over the PIC (0x20), the RTC and its neighbours (0x6E to 0x73, by byte and as
 *        a double word from 0x6E) and the last DMA port (a word from 0x0F); methods read and write them,
 *        and ask `\_OSI` for "Windows 2000" (A200) and "Windows 2001" (A201).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (PIC, SystemIO, 0x20, 0x02)
    Field (PIC, ByteAcc, NoLock, Preserve)
    {
        P020,   8
    }

    OperationRegion (RTC, SystemIO, 0x6E, 0x06)
    Field (RTC, ByteAcc, NoLock, Preserve)
    {
        P06E,   8,
        P06F,   8,
        P070,   8,
        P071,   8,
        P072,   8
    }

    Field (RTC, DWordAcc, NoLock, Preserve)
    {
        D06E,   32
    }

    OperationRegion (DMA, SystemIO, 0x0F, 0x02)
    Field (DMA, WordAcc, NoLock, Preserve)
    {
        W00F,   16
    }

    Method (RTCR, 0, NotSerialized)
    {
        Return (P070)
    }

    Method (WRTC, 0, NotSerialized)
    {
        P070 = 0x33
    }

    Method (NEIG, 0, NotSerialized)
    {
        Return ((P072 | (P06F << 0x08)))
    }

    Method (DWRD, 0, NotSerialized)
    {
        Return (D06E)
    }

    Method (DMAW, 0, NotSerialized)
    {
        Return (W00F)
    }

    Method (PICR, 0, NotSerialized)
    {
        Return (P020)
    }

    Method (A200, 0, NotSerialized)
    {
        Return (_OSI ("Windows 2000"))
    }

    Method (A201, 0, NotSerialized)
    {
        Return (_OSI ("Windows 2001"))
    }
}
