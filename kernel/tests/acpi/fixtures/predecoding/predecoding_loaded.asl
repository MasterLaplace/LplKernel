// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The table XDEV.MAIN's loop loads, 50 bytes as an SSDT: `Scope (\XDEV) { Name (VALU, 100) }`.
 */
DefinitionBlock ("", "SSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Scope (\XDEV) __EXPECT__(6161)
    {
        Name (VALU, 0x64)
    }
}
