// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief One object of each kind a result holds: INT_ an Integer, STR_ a String, BUF_ a Buffer, PKG_ a Package of an
 *        Integer, a String, a nested Package and a name, DEV_ a Device, which holds no data, and MTH_ a method that
 *        returns INT_ plus one.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (INT_, 0x89ABCDEF)
    Name (STR_, "ab")
    Name (BUF_, Buffer (3) { 0x01, 0x02, 0x03 })
    Name (PKG_, Package (4) { 0x05, "c", Package (1) { 0x06 }, INT_ })
    Device (DEV_)
    {
        Name (_ADR, Zero)
    }

    Method (MTH_, 0, NotSerialized)
    {
        Return (Add (INT_, One))
    }
}
