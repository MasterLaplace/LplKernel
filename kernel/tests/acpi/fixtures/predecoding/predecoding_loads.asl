// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A table the loop loads that masks the name it reads: VALU holds 1 at the root, TBLR is a
 *        SystemMemory region of 0x32 bytes at PREDECODING_LOADED_ADDRESS, and \XDEV.MAIN runs `Local0 = 0;
 *        Local1 = 0; While (Local0 < 4) { Local0++; If (Local0 == 3) { Load (TBLR, HNDL) }; Local1 += VALU };
 *        Return (Local1)`: 1 + 1 + 100 + 100, so 202, once the table declares \XDEV.VALU, 100.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (VALU, One)
    Name (HNDL, Zero)
    OperationRegion (TBLR, SystemMemory, 0x00300000, 0x32)
    Device (XDEV) __EXPECT__(3141)
    {
        Method (MAIN, 0, NotSerialized)
        {
            Local0 = Zero
            Local1 = Zero
            While ((Local0 < 0x04))
            {
                Local0++
                If ((Local0 == 0x03))
                {
                    Load (TBLR, HNDL)
                }

                Local1 += VALU
            }

            Return (Local1)
        }
    }
}
