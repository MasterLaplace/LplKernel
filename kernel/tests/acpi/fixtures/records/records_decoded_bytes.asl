// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The bytes a load decodes: INT_ an integer the declaration pass builds, BUF_ a Buffer it keeps in its table,
 *        its initialiser stepped over, DEV_ a Device whose body the interpreter hands back to the declaration pass, and
 *        LOOP a method whose While loops three times, so that its statements run from records.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (INT_, 0x05)
    Name (BUF_, Buffer (2) { 0x01, 0x02 })
    Device (DEV_)
    {
        Name (_ADR, Zero)
    }

    Method (LOOP, 0, NotSerialized)
    {
        Store (Zero, Local0)
        While (LLess (Local0, 0x03))
        {
            Increment (Local0)
        }
        Return (Local0)
    }
}
