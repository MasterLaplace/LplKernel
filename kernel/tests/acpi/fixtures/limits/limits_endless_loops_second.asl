// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The SSDT after the endless loops: While (CNT2 < 0x20) increments CNT2.
 */
DefinitionBlock ("", "SSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (CNT2, Zero)
    While ((CNT2 < 0x20))
    {
        CNT2++
    }
}
