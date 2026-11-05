// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A While at offset 0 as a statement alone: `While (Local0 < 1000) { Local0++ }`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    While ((Local0 < 0x03E8))
    {
        Local0++
    }
}
