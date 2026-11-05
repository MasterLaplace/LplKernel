// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Intel's pattern for HWP: GNVS, an NVS region at 0x320000, holds HWPA and HWPL, where the HWP table lies and
 *        its length, and GRNT, the bits the platform grants unasked; \_SB._OSC grants CDW2 | GRNT and keeps it in
 *        OSCP; CPU0's _PDC calls GCAP, which Loads the HWP table from HWPA and HWPL only when OSCP holds bit 6.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (GNVS, SystemMemory, 0x00320000, 0x0C)
    Field (GNVS, DWordAcc, NoLock, Preserve)
    {
        HWPA,   32,
        HWPL,   32,
        GRNT,   32
    }
    Name (OSCP, Zero)
    Name (HWPH, Zero)
    Scope (\_SB)
    {
        Method (_OSC, 4, Serialized)
        {
            CreateDWordField (Arg3, Zero, CDW1)
            CreateDWordField (Arg3, 0x04, CDW2)
            Store (Or (CDW2, GRNT), CDW2)
            Store (CDW2, OSCP)
            Return (Arg3)
        }
    }
    Processor (CPU0, 0x01, 0x00000410, 0x06) __EXPECT__(3168)
    {
        Method (GCAP, 1, Serialized)
        {
            If (And (OSCP, 0x40))
            {
                OperationRegion (HWPR, SystemMemory, HWPA, HWPL)
                Load (HWPR, HWPH)
            }
        }
        Method (_PDC, 1, Serialized)
        {
            GCAP (Arg0)
        }
    }
}
