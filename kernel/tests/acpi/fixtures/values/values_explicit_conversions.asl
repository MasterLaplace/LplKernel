// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief ToDecimalString, ToHexString, ToString, ToBuffer and ToInteger of Integers, Buffers and Strings, each
 *        stored to a Name.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (S000, 0x00)
    Name (S001, 0x00)
    Name (S002, 0x00)
    Name (S003, 0x00)
    Name (S004, 0x00)
    Name (S005, 0x00)
    Name (S006, 0x00)
    Name (S007, 0x00)
    Name (S008, 0x00)
    Name (B000, 0x00)
    Name (B001, 0x00)
    Name (B002, 0x00)
    Name (I000, 0x07)
    Name (I001, 0x07)
    Name (I002, 0x07)
    Name (I003, 0x07)
    Name (I004, 0x07)
    Name (I005, 0x07)
    Name (I006, 0x07)
    Name (I007, 0x07)
    CopyObject (ToDecimalString (0x04D2), S000)
    CopyObject (ToDecimalString (Buffer (0x02)
            {
                 0x07, 0xC8
            }), S001)
    CopyObject (ToHexString (0x2A), S002)
    CopyObject (ToHexString (Buffer (0x02)
            {
                 0x0A, 0xFF
            }), S003)
    CopyObject (ToHexString ("XY"), S004)
    CopyObject (ToString (Buffer (0x04)
            {
                 0x41, 0x42, 0x00, 0x43
            }, Ones), S005)
    CopyObject (ToString (Buffer (0x03)
            {
                 0x41, 0x42, 0x43
            }, 0x02), S006)
    CopyObject (ToDecimalString (Buffer (0x00){}), S007)
    CopyObject (ToHexString (0x00), S008)
    CopyObject (ToBuffer ("HI"), B000)
    CopyObject (ToBuffer (0x0102), B001)
    CopyObject (ToBuffer (Buffer (0x01)
            {
                 0x05
            }), B002)
    I000 = ToInteger ("  42")
    I001 = ToInteger ("0x2A")
    I002 = ToInteger ("052")
    I003 = ToInteger ("-1")
    I004 = ToInteger ("99999999999999999999999")
    I005 = ToInteger (Buffer (0x02)
            {
                "*"
            })
    I006 = ToInteger ("42ABC")
    I007 = ToInteger (0x2A)
}
