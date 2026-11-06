// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The body of an attach method whose package element names a Name that refers back to that element, so that
 *        reading it resolves names past the cap of 8 (§6.1).
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (ALSL, 0x00)
    Name (PKGL, Package (0x01)
    {
        ALSL
    })
    CopyObject (PKGL [0x00], ALSL)
    Return (DerefOf (PKGL [0x00]))
}
