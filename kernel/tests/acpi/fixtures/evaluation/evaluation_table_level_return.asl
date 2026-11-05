// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief BEFR, a Return (One) at table level, outside any method, then AFTR.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BEFR, One)
    Return (One)
    Name (AFTR, One)
}
