// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MNOR (), MCON (), MPFX () and MPFC (): the calls of the methods whose field lists the test refuses
 *        or passes over.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    MNOR () __EXPECT__(6084)
    MCON () __EXPECT__(6084)
    MPFX () __EXPECT__(6084)
    MPFC () __EXPECT__(6084)
}
