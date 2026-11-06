// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Devices of \PCI0 whose _ADR reads a unit of the next one's PCI_Config region first: DEVA reads DEVB's, which
 *        reads DEVC's; DEVD reads DEVE's, which table-level code reads into RES_; RDEV's _REG reads DEVB's, and its
 *        MKRG creates the PCI_Config region DYNR in RDEV.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (RES_, 7)
    Name (READ, 0)
    Device (PCI0)
    {
        Name (_HID, EisaId ("PNP0A03"))
        Device (DEVA)
        {
            Method (_ADR)
            {
                READ = \PCI0.DEVB.VID_
                Return (0x00030001)
            }
            OperationRegion (PCFG, PCI_Config, 0, 0x0100)
            Field (PCFG, ByteAcc, NoLock, Preserve)
            {
                VID_, 16
            }
        }
        Device (DEVB)
        {
            Method (_ADR)
            {
                READ = \PCI0.DEVC.VID_
                Return (0x00030001)
            }
            OperationRegion (PCFG, PCI_Config, 0, 0x0100)
            Field (PCFG, ByteAcc, NoLock, Preserve)
            {
                VID_, 16
            }
        }
        Device (DEVC)
        {
            Method (_ADR)
            {
                Return (0x00030001)
            }
            OperationRegion (PCFG, PCI_Config, 0, 0x0100)
            Field (PCFG, ByteAcc, NoLock, Preserve)
            {
                VID_, 16
            }
        }
        Device (DEVD)
        {
            Method (_ADR)
            {
                READ = \PCI0.DEVE.VID_
                Return (0x00030001)
            }
            OperationRegion (PCFG, PCI_Config, 0, 0x0100)
            Field (PCFG, ByteAcc, NoLock, Preserve)
            {
                VID_, 16
            }
        }
        Device (DEVE)
        {
            Method (_ADR)
            {
                Return (0x00030001)
            }
            OperationRegion (PCFG, PCI_Config, 0, 0x0100)
            Field (PCFG, ByteAcc, NoLock, Preserve)
            {
                VID_, 16
            }
        }
    }
    Device (RDEV) __EXPECT__(3141)
    {
        Method (_REG, 2) __EXPECT__(3079)
        {
            READ = \PCI0.DEVB.VID_
        }
        Method (MKRG)
        {
            OperationRegion (^DYNR, PCI_Config, 0, 0x10) __EXPECT__(3175)
        }
    }
    RES_ = \PCI0.DEVD.VID_
}
