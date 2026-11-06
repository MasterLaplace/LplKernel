// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of a _REG (1): \TRAC takes 0xF as its last hexadecimal digit.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (Multiply (\TRAC, 0x10), 0x0F), \TRAC) __EXPECT__(6084)
}
