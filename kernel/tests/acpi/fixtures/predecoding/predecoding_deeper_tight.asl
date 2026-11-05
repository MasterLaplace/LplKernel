// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Tight loops whose body runs first in a call deeper than the one that linked it, as PKTL: `Local0 = INNR ();
 *        Return (Local0 + (1 + (1 + INNR ())))`, INNR running `Local0 = 0; Local1 = 0; While (Local0 < 3) { While
 *        (Local1 < Local0) { Local1 += 1 }; Local0++ }; Return (Local1)`: the second call enters its tight loops at
 *        once, their records linked by the first, two values deeper, so 6.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (INNR, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x03))
        {
            While ((Local1 < Local0))
            {
                Local1 += One
            }

            Local0++
        }

        Return (Local1)
    }

    Method (PKTL, 0, NotSerialized)
    {
        Local0 = INNR ()
        Return ((Local0 + (One + (One + INNR ()))))
    }
}
