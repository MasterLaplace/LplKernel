// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (Add (Multiply (\TRAC, 0x10), 0x01), \TRAC).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (Multiply (\TRAC, 0x10), 0x01), \TRAC) __EXPECT__(6084)
}
