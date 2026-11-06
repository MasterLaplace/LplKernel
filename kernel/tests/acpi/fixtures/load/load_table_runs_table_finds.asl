// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief \RES1 = LoadTable ("OEM1", "LPLTST", "MADEUPML", "", "", Zero).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \RES1 = LoadTable ("OEM1", "LPLTST", "MADEUPML", "", "", Zero) __EXPECT__(6084)
}
