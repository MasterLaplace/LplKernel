// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief An SSDT whose method WIDE counts three turns where `Ones == 0xFFFFFFFF`: none under 64-bit
 *        integers, three under 32-bit ones.
 */
DefinitionBlock ("", "SSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (WIDE, 0, NotSerialized)
    {
        Local0 = Zero
        Local1 = Zero
        While ((Local1 < 0x03))
        {
            Local1++
            If ((Ones == 0xFFFFFFFF))
            {
                Local0++
            }
        }

        Return (Local0)
    }
}
