// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (\PCI0.DEV_.VID_, \V1__): a read of the vendor field of a PCI_Config region.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    \V1 = \PCI0.DEV.VID __EXPECT__(6084)
}
