// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (Add (Multiply (Add (Multiply (\TRAC, 0x10), Arg0), 0x10), Arg1), \TRAC): the body of a _REG (2), \TRAC
 *        taking Arg0 then Arg1 as its two last hexadecimal digits.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (Multiply (Add (Multiply (\TRAC, 0x10), Arg0), 0x10), Arg1), \TRAC) __EXPECT__(6084)
}
