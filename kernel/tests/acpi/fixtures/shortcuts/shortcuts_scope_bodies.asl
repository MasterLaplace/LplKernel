// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Table-level code inside the TermLists of the scopes a table declares, nested: in `\_SB`, the Devices `D0__`
 *        and `D1__`, whose TermList holds a taken If around `D2__`, an OperationRegion whose offset is a name, `Store`,
 *        `D3__` around an If whose Else runs, then an If whose predicate fails around `D8__`; `D4__`, empty; `D5__`
 *        around `D6__` around `D7__`, which end together; `TZ0_`; `D9__` around `DA__`, then a Name; then
 *        `Scope (\_SB.D0__)` and a Name at the root: 13 TermLists the declaration pass enters while it runs the
 *        table's code, 9 of which it leaves itself, `D8__`'s declared without running.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    External (\UNDF, IntObj)

    Name (N0, One)
    Scope (\_SB)
    {
        Device (D0) __EXPECT__(3141)
        {
            Name (N1, 0x10)
            Device (D1) __EXPECT__(3141)
            {
                Name (N2, 0x02)
                If (N0)
                {
                    Name (N3, 0x03)
                    Device (D2) __EXPECT__(3141)
                    {
                        Name (N4, 0x04)
                    }

                    Name (N5, 0x05)
                }

                OperationRegion (R0, SystemMemory, N1, 0x10)
                Field (R0, ByteAcc, NoLock, Preserve)
                {
                    F0,   8
                }

                N0 = 0x02
                Device (D3) __EXPECT__(3141)
                {
                    If (Zero)
                    {
                        Name (N6, 0x06)
                    }
                    Else
                    {
                        Name (N7, 0x07)
                    }

                    Name (N8, 0x08)
                }

                If (UNDF)
                {
                    Device (D8) __EXPECT__(3141)
                    {
                        Name (NF, 0x0F)
                    }
                }
            }

            Name (N9, 0x09)
            Device (D4) __EXPECT__(3141)
            {
            }
        }

        Device (D5) __EXPECT__(3141)
        {
            Device (D6) __EXPECT__(3141)
            {
                Device (D7) __EXPECT__(3141)
                {
                    Name (NA, 0x0A)
                }
            }
        }

        ThermalZone (TZ0)
        {
            Name (NB, 0x0B)
        }

        Device (D9) __EXPECT__(3141)
        {
            Device (DA) __EXPECT__(3141)
            {
                Name (NC, 0x0C)
            }

            Name (NG, 0x10)
        }
    }

    Scope (\_SB.D0)
    {
        Name (ND, 0x0D)
    }

    Name (NE, 0x0E)
}
