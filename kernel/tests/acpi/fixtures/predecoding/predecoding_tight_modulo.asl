// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A tight loop whose body divides by zero at its fifth turn, as MODT: `Local0 = 0; While (Local0 <
 *        10) { Local0 += 1 + (0 % (4 - Local0)) }; Return (Local0)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (MODT, 0, NotSerialized)
    {
        Local0 = Zero
        While ((Local0 < 0x0A))
        {
            Local0 += (One + (Zero % (0x04 - Local0)))
        }

        Return (Local0)
    }
}
