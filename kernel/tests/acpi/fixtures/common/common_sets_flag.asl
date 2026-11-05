// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (One, \FLAG): the body of an _INI that makes present a _STA that reads \FLAG.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \FLAG = One __EXPECT__(6084)
}
