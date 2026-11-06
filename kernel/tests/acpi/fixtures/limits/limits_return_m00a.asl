// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Return (M00A): the end of the body of M009, which calls the method a test sizes at run time.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Return (M00A ()) __EXPECT__(6084)
}
