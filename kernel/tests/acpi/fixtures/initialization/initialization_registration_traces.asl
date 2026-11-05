// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of _REG (2): \TRAC takes Arg0 then Arg1 as its two last hexadecimal digits.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (Multiply (Add (Multiply (\TRAC, 0x10), Arg0), 0x10), Arg1), \TRAC) __EXPECT__(6084)
}
