// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (Add (One, Add (2, \PCI0.DEV_.VID_)), \V2__): a read of the vendor field nested two operators deep.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Add (One, Add (0x02, \PCI0.DEV.VID)), \V2) __EXPECT__(6084)
}
