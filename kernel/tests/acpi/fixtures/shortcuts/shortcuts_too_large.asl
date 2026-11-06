// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A Buffer too large for an arena with no room left, then what follows it: `Name (BIG_, Buffer (0x8000) {})`,
 *        `Name (INT_, 5)`, `Method (MBIG) {Return (SizeOf (BIG_))}` and `Name (LAST, One)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BIG, Buffer (0x8000){})
    Name (INT, 0x05)
    Method (MBIG, 0, NotSerialized)
    {
        Return (SizeOf (BIG))
    }

    Name (LAST, One)
}
