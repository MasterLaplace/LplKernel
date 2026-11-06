// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A table-level buffer field over a Buffer too large for an arena with no room left, then a name:
 *        `Name (BIG_, Buffer (0x8000) {})`, `CreateByteField (BIG_, Zero, BYT_)` and `Name (LAST, One)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BIG, Buffer (0x8000){})
    CreateByteField (BIG, Zero, BYT)
    Name (LAST, One)
}
