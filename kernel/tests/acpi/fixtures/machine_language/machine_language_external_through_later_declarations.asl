// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Alias (\_SB, \ALS_) and Name (\_SB.FOO_, One), the object an External reaches through the Alias.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Alias (\_SB, \ALS)
    Name (\_SB.FOO, One)
}
