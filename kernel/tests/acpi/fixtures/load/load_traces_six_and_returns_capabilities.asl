// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief An _OSC that appends the digit 6 to the trace, then gives back the capabilities Buffer it was given.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (Multiply (\TRAC, 0x10), 0x06), \TRAC) __EXPECT__(6084)
    Return (Arg3)
}
