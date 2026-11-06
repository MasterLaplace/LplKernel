// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief BUF_, a Buffer of 8 bytes, and a CreateDWordField, a CreateBitField and a CreateField of 12 bits over
 *        it.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (BUF, Buffer (0x08){})
    CreateDWordField (BUF, 0x02, DW)
    CreateBitField (BUF, 0x03, BIT)
    CreateField (BUF, 0x04, 0x0C, CF)
}
