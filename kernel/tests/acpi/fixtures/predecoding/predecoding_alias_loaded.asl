// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The table the loop of predecoding_alias_fills loads, as an SSDT: `Alias (\DEVB, \FOO_)`.
 */
DefinitionBlock ("", "SSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Alias (\DEVB, \FOO) __EXPECT__(6084) __EXPECT__(6161)
}
