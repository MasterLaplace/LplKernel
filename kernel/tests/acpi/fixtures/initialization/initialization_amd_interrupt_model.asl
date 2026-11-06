// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief AMD's interrupt model: PIRA to PIRD, the links of the PCI interrupt router, behind its index and data ports,
 *        0xC00 and 0xC01; _PIC (Arg0) stores Arg0 to PICM and, when it is 1, the APIC's model, writes 0x1F to each
 *        link, which routes none of them through the 8259.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (PIRQ, SystemIO, 0x0C00, 0x02)
    Field (PIRQ, ByteAcc, NoLock, Preserve)
    {
        PIDX,   8,
        PDAT,   8
    }
    IndexField (PIDX, PDAT, ByteAcc, NoLock, Preserve)
    {
        PIRA,   8,
        PIRB,   8,
        PIRC,   8,
        PIRD,   8
    }
    Name (PICM, Zero)
    Method (_PIC, 1, NotSerialized)
    {
        Store (Arg0, PICM)
        If (LEqual (Arg0, One))
        {
            Store (0x1F, PIRA)
            Store (0x1F, PIRB)
            Store (0x1F, PIRC)
            Store (0x1F, PIRD)
        }
    }
}
