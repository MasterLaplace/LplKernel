// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A While whose body is a Break, as BRKB (Arg0): `Local0 = 0; Local1 = 0; While (Local0 < 4) { Local0++; While
 *        (Local1 < 5) { Break } }; Return (Arg0)`: never a tight loop.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (BRKB, 1, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local0 < 0x04))
        {
            Local0++
            While ((Local1 < 0x05))
            {
                Break
            }
        }

        Return (Arg0)
    }
}
