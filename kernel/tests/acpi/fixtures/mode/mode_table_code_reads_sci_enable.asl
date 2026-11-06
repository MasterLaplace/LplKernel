// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Table-level code that reads SCI_EN, bit 0 of PM1a_CNT at port 0x404, through a SystemIO field and stores
 *        it in SCIS, 2 before it runs.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (PMCT, SystemIO, 0x0404, 0x02)
    Field (PMCT, WordAcc, NoLock, Preserve)
    {
        SCIE,   1
    }

    Name (SCIS, 0x02)
    SCIS = SCIE
}
