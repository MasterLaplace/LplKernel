// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief \HND1 = LoadTable ("OEM1", "", "", "", "", One).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \HND1 = LoadTable ("OEM1", "", "", "", "", One) __EXPECT__(6084)
}
