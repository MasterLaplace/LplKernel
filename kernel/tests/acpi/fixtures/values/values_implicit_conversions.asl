// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Methods whose operators take, where they need an Integer, Strings with and without a 0x prefix, a Buffer,
 *        an empty String, an empty Buffer and a Package.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (R000, 0x07)
    Name (R001, 0x07)
    Name (R002, 0x07)
    Name (R003, 0x07)
    Name (R004, 0x07)
    Name (R005, 0x07)
    Name (R006, 0x07)
    Name (R007, 0x07)
    Store (Add ("1F", 0x01), R000)
    Store (Add (Buffer (0x02)
            {
                 0x01, 0x02
            }, 0x00), R001)
    Store (Add ("0x10", 0x00), R002)
    Store (Add ("", 0x05), R003)
    Store (Add ("12G4", 0x00), R004)
    Store (Add (Package (0x01) __EXPECT__(6058)
            {
                0x01
            }, 0x00), R005)
    Store (Add (Buffer (0x09)
            {
                  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                  0x09
            }, 0x00), R006)
    Store (Add (Buffer (0x00){}, 0x03), R007)
}
