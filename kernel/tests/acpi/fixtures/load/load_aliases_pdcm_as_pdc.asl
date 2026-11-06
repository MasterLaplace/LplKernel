// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Alias (\PDCM, _PDC): a _PDC that names a method another table declared before it.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    External (\PDCM, MethodObj)

    Alias (\PDCM, _PDC)
}
