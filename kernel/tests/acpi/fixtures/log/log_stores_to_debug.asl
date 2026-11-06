// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Stores to Debug an Integer, a String whose space the line writes as _, and a Buffer of two bytes.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Debug = 0x2A
    Debug = "Hi there"
    Debug = Buffer (0x02)
        {
             0x01, 0xFE
        }
}
