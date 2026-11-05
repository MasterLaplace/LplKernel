// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Table-level loops: a While increments CNT_ up to 3; While (One) increments BRK_, breaks, then would increment
 *        it again; a While increments CNU_ up to 2, continuing before it increments SKP_.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (CNT, Zero)
    While ((CNT < 0x03))
    {
        CNT++
    }

    Name (BRK, Zero)
    While (One)
    {
        BRK++
        Break
        BRK++
    }

    Name (CNU, Zero)
    Name (SKP, Zero)
    While ((CNU < 0x02))
    {
        CNU++
        Continue
        SKP++
    }
}
