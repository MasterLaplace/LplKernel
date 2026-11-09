// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A root bridge with 257 children whose _ADR names a PCI function: the 256 functions of a bus, B000 to B0FF,
 *        then B100, which names function 0 of device 0 again and comes past the ones a boot compares. B000 to B040
 *        each hold an empty _PRT: the 65th, B040, comes past the bridges a boot publishes.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Device (B000) { Name (_ADR, Zero) Name (_PRT, Package (0x00) {}) }
            Device (B001) { Name (_ADR, 0x00000001) Name (_PRT, Package (0x00) {}) }
            Device (B002) { Name (_ADR, 0x00000002) Name (_PRT, Package (0x00) {}) }
            Device (B003) { Name (_ADR, 0x00000003) Name (_PRT, Package (0x00) {}) }
            Device (B004) { Name (_ADR, 0x00000004) Name (_PRT, Package (0x00) {}) }
            Device (B005) { Name (_ADR, 0x00000005) Name (_PRT, Package (0x00) {}) }
            Device (B006) { Name (_ADR, 0x00000006) Name (_PRT, Package (0x00) {}) }
            Device (B007) { Name (_ADR, 0x00000007) Name (_PRT, Package (0x00) {}) }
            Device (B008) { Name (_ADR, 0x00010000) Name (_PRT, Package (0x00) {}) }
            Device (B009) { Name (_ADR, 0x00010001) Name (_PRT, Package (0x00) {}) }
            Device (B00A) { Name (_ADR, 0x00010002) Name (_PRT, Package (0x00) {}) }
            Device (B00B) { Name (_ADR, 0x00010003) Name (_PRT, Package (0x00) {}) }
            Device (B00C) { Name (_ADR, 0x00010004) Name (_PRT, Package (0x00) {}) }
            Device (B00D) { Name (_ADR, 0x00010005) Name (_PRT, Package (0x00) {}) }
            Device (B00E) { Name (_ADR, 0x00010006) Name (_PRT, Package (0x00) {}) }
            Device (B00F) { Name (_ADR, 0x00010007) Name (_PRT, Package (0x00) {}) }
            Device (B010) { Name (_ADR, 0x00020000) Name (_PRT, Package (0x00) {}) }
            Device (B011) { Name (_ADR, 0x00020001) Name (_PRT, Package (0x00) {}) }
            Device (B012) { Name (_ADR, 0x00020002) Name (_PRT, Package (0x00) {}) }
            Device (B013) { Name (_ADR, 0x00020003) Name (_PRT, Package (0x00) {}) }
            Device (B014) { Name (_ADR, 0x00020004) Name (_PRT, Package (0x00) {}) }
            Device (B015) { Name (_ADR, 0x00020005) Name (_PRT, Package (0x00) {}) }
            Device (B016) { Name (_ADR, 0x00020006) Name (_PRT, Package (0x00) {}) }
            Device (B017) { Name (_ADR, 0x00020007) Name (_PRT, Package (0x00) {}) }
            Device (B018) { Name (_ADR, 0x00030000) Name (_PRT, Package (0x00) {}) }
            Device (B019) { Name (_ADR, 0x00030001) Name (_PRT, Package (0x00) {}) }
            Device (B01A) { Name (_ADR, 0x00030002) Name (_PRT, Package (0x00) {}) }
            Device (B01B) { Name (_ADR, 0x00030003) Name (_PRT, Package (0x00) {}) }
            Device (B01C) { Name (_ADR, 0x00030004) Name (_PRT, Package (0x00) {}) }
            Device (B01D) { Name (_ADR, 0x00030005) Name (_PRT, Package (0x00) {}) }
            Device (B01E) { Name (_ADR, 0x00030006) Name (_PRT, Package (0x00) {}) }
            Device (B01F) { Name (_ADR, 0x00030007) Name (_PRT, Package (0x00) {}) }
            Device (B020) { Name (_ADR, 0x00040000) Name (_PRT, Package (0x00) {}) }
            Device (B021) { Name (_ADR, 0x00040001) Name (_PRT, Package (0x00) {}) }
            Device (B022) { Name (_ADR, 0x00040002) Name (_PRT, Package (0x00) {}) }
            Device (B023) { Name (_ADR, 0x00040003) Name (_PRT, Package (0x00) {}) }
            Device (B024) { Name (_ADR, 0x00040004) Name (_PRT, Package (0x00) {}) }
            Device (B025) { Name (_ADR, 0x00040005) Name (_PRT, Package (0x00) {}) }
            Device (B026) { Name (_ADR, 0x00040006) Name (_PRT, Package (0x00) {}) }
            Device (B027) { Name (_ADR, 0x00040007) Name (_PRT, Package (0x00) {}) }
            Device (B028) { Name (_ADR, 0x00050000) Name (_PRT, Package (0x00) {}) }
            Device (B029) { Name (_ADR, 0x00050001) Name (_PRT, Package (0x00) {}) }
            Device (B02A) { Name (_ADR, 0x00050002) Name (_PRT, Package (0x00) {}) }
            Device (B02B) { Name (_ADR, 0x00050003) Name (_PRT, Package (0x00) {}) }
            Device (B02C) { Name (_ADR, 0x00050004) Name (_PRT, Package (0x00) {}) }
            Device (B02D) { Name (_ADR, 0x00050005) Name (_PRT, Package (0x00) {}) }
            Device (B02E) { Name (_ADR, 0x00050006) Name (_PRT, Package (0x00) {}) }
            Device (B02F) { Name (_ADR, 0x00050007) Name (_PRT, Package (0x00) {}) }
            Device (B030) { Name (_ADR, 0x00060000) Name (_PRT, Package (0x00) {}) }
            Device (B031) { Name (_ADR, 0x00060001) Name (_PRT, Package (0x00) {}) }
            Device (B032) { Name (_ADR, 0x00060002) Name (_PRT, Package (0x00) {}) }
            Device (B033) { Name (_ADR, 0x00060003) Name (_PRT, Package (0x00) {}) }
            Device (B034) { Name (_ADR, 0x00060004) Name (_PRT, Package (0x00) {}) }
            Device (B035) { Name (_ADR, 0x00060005) Name (_PRT, Package (0x00) {}) }
            Device (B036) { Name (_ADR, 0x00060006) Name (_PRT, Package (0x00) {}) }
            Device (B037) { Name (_ADR, 0x00060007) Name (_PRT, Package (0x00) {}) }
            Device (B038) { Name (_ADR, 0x00070000) Name (_PRT, Package (0x00) {}) }
            Device (B039) { Name (_ADR, 0x00070001) Name (_PRT, Package (0x00) {}) }
            Device (B03A) { Name (_ADR, 0x00070002) Name (_PRT, Package (0x00) {}) }
            Device (B03B) { Name (_ADR, 0x00070003) Name (_PRT, Package (0x00) {}) }
            Device (B03C) { Name (_ADR, 0x00070004) Name (_PRT, Package (0x00) {}) }
            Device (B03D) { Name (_ADR, 0x00070005) Name (_PRT, Package (0x00) {}) }
            Device (B03E) { Name (_ADR, 0x00070006) Name (_PRT, Package (0x00) {}) }
            Device (B03F) { Name (_ADR, 0x00070007) Name (_PRT, Package (0x00) {}) }
            Device (B040) { Name (_ADR, 0x00080000) Name (_PRT, Package (0x00) {}) }
            Device (B041) { Name (_ADR, 0x00080001) }
            Device (B042) { Name (_ADR, 0x00080002) }
            Device (B043) { Name (_ADR, 0x00080003) }
            Device (B044) { Name (_ADR, 0x00080004) }
            Device (B045) { Name (_ADR, 0x00080005) }
            Device (B046) { Name (_ADR, 0x00080006) }
            Device (B047) { Name (_ADR, 0x00080007) }
            Device (B048) { Name (_ADR, 0x00090000) }
            Device (B049) { Name (_ADR, 0x00090001) }
            Device (B04A) { Name (_ADR, 0x00090002) }
            Device (B04B) { Name (_ADR, 0x00090003) }
            Device (B04C) { Name (_ADR, 0x00090004) }
            Device (B04D) { Name (_ADR, 0x00090005) }
            Device (B04E) { Name (_ADR, 0x00090006) }
            Device (B04F) { Name (_ADR, 0x00090007) }
            Device (B050) { Name (_ADR, 0x000A0000) }
            Device (B051) { Name (_ADR, 0x000A0001) }
            Device (B052) { Name (_ADR, 0x000A0002) }
            Device (B053) { Name (_ADR, 0x000A0003) }
            Device (B054) { Name (_ADR, 0x000A0004) }
            Device (B055) { Name (_ADR, 0x000A0005) }
            Device (B056) { Name (_ADR, 0x000A0006) }
            Device (B057) { Name (_ADR, 0x000A0007) }
            Device (B058) { Name (_ADR, 0x000B0000) }
            Device (B059) { Name (_ADR, 0x000B0001) }
            Device (B05A) { Name (_ADR, 0x000B0002) }
            Device (B05B) { Name (_ADR, 0x000B0003) }
            Device (B05C) { Name (_ADR, 0x000B0004) }
            Device (B05D) { Name (_ADR, 0x000B0005) }
            Device (B05E) { Name (_ADR, 0x000B0006) }
            Device (B05F) { Name (_ADR, 0x000B0007) }
            Device (B060) { Name (_ADR, 0x000C0000) }
            Device (B061) { Name (_ADR, 0x000C0001) }
            Device (B062) { Name (_ADR, 0x000C0002) }
            Device (B063) { Name (_ADR, 0x000C0003) }
            Device (B064) { Name (_ADR, 0x000C0004) }
            Device (B065) { Name (_ADR, 0x000C0005) }
            Device (B066) { Name (_ADR, 0x000C0006) }
            Device (B067) { Name (_ADR, 0x000C0007) }
            Device (B068) { Name (_ADR, 0x000D0000) }
            Device (B069) { Name (_ADR, 0x000D0001) }
            Device (B06A) { Name (_ADR, 0x000D0002) }
            Device (B06B) { Name (_ADR, 0x000D0003) }
            Device (B06C) { Name (_ADR, 0x000D0004) }
            Device (B06D) { Name (_ADR, 0x000D0005) }
            Device (B06E) { Name (_ADR, 0x000D0006) }
            Device (B06F) { Name (_ADR, 0x000D0007) }
            Device (B070) { Name (_ADR, 0x000E0000) }
            Device (B071) { Name (_ADR, 0x000E0001) }
            Device (B072) { Name (_ADR, 0x000E0002) }
            Device (B073) { Name (_ADR, 0x000E0003) }
            Device (B074) { Name (_ADR, 0x000E0004) }
            Device (B075) { Name (_ADR, 0x000E0005) }
            Device (B076) { Name (_ADR, 0x000E0006) }
            Device (B077) { Name (_ADR, 0x000E0007) }
            Device (B078) { Name (_ADR, 0x000F0000) }
            Device (B079) { Name (_ADR, 0x000F0001) }
            Device (B07A) { Name (_ADR, 0x000F0002) }
            Device (B07B) { Name (_ADR, 0x000F0003) }
            Device (B07C) { Name (_ADR, 0x000F0004) }
            Device (B07D) { Name (_ADR, 0x000F0005) }
            Device (B07E) { Name (_ADR, 0x000F0006) }
            Device (B07F) { Name (_ADR, 0x000F0007) }
            Device (B080) { Name (_ADR, 0x00100000) }
            Device (B081) { Name (_ADR, 0x00100001) }
            Device (B082) { Name (_ADR, 0x00100002) }
            Device (B083) { Name (_ADR, 0x00100003) }
            Device (B084) { Name (_ADR, 0x00100004) }
            Device (B085) { Name (_ADR, 0x00100005) }
            Device (B086) { Name (_ADR, 0x00100006) }
            Device (B087) { Name (_ADR, 0x00100007) }
            Device (B088) { Name (_ADR, 0x00110000) }
            Device (B089) { Name (_ADR, 0x00110001) }
            Device (B08A) { Name (_ADR, 0x00110002) }
            Device (B08B) { Name (_ADR, 0x00110003) }
            Device (B08C) { Name (_ADR, 0x00110004) }
            Device (B08D) { Name (_ADR, 0x00110005) }
            Device (B08E) { Name (_ADR, 0x00110006) }
            Device (B08F) { Name (_ADR, 0x00110007) }
            Device (B090) { Name (_ADR, 0x00120000) }
            Device (B091) { Name (_ADR, 0x00120001) }
            Device (B092) { Name (_ADR, 0x00120002) }
            Device (B093) { Name (_ADR, 0x00120003) }
            Device (B094) { Name (_ADR, 0x00120004) }
            Device (B095) { Name (_ADR, 0x00120005) }
            Device (B096) { Name (_ADR, 0x00120006) }
            Device (B097) { Name (_ADR, 0x00120007) }
            Device (B098) { Name (_ADR, 0x00130000) }
            Device (B099) { Name (_ADR, 0x00130001) }
            Device (B09A) { Name (_ADR, 0x00130002) }
            Device (B09B) { Name (_ADR, 0x00130003) }
            Device (B09C) { Name (_ADR, 0x00130004) }
            Device (B09D) { Name (_ADR, 0x00130005) }
            Device (B09E) { Name (_ADR, 0x00130006) }
            Device (B09F) { Name (_ADR, 0x00130007) }
            Device (B0A0) { Name (_ADR, 0x00140000) }
            Device (B0A1) { Name (_ADR, 0x00140001) }
            Device (B0A2) { Name (_ADR, 0x00140002) }
            Device (B0A3) { Name (_ADR, 0x00140003) }
            Device (B0A4) { Name (_ADR, 0x00140004) }
            Device (B0A5) { Name (_ADR, 0x00140005) }
            Device (B0A6) { Name (_ADR, 0x00140006) }
            Device (B0A7) { Name (_ADR, 0x00140007) }
            Device (B0A8) { Name (_ADR, 0x00150000) }
            Device (B0A9) { Name (_ADR, 0x00150001) }
            Device (B0AA) { Name (_ADR, 0x00150002) }
            Device (B0AB) { Name (_ADR, 0x00150003) }
            Device (B0AC) { Name (_ADR, 0x00150004) }
            Device (B0AD) { Name (_ADR, 0x00150005) }
            Device (B0AE) { Name (_ADR, 0x00150006) }
            Device (B0AF) { Name (_ADR, 0x00150007) }
            Device (B0B0) { Name (_ADR, 0x00160000) }
            Device (B0B1) { Name (_ADR, 0x00160001) }
            Device (B0B2) { Name (_ADR, 0x00160002) }
            Device (B0B3) { Name (_ADR, 0x00160003) }
            Device (B0B4) { Name (_ADR, 0x00160004) }
            Device (B0B5) { Name (_ADR, 0x00160005) }
            Device (B0B6) { Name (_ADR, 0x00160006) }
            Device (B0B7) { Name (_ADR, 0x00160007) }
            Device (B0B8) { Name (_ADR, 0x00170000) }
            Device (B0B9) { Name (_ADR, 0x00170001) }
            Device (B0BA) { Name (_ADR, 0x00170002) }
            Device (B0BB) { Name (_ADR, 0x00170003) }
            Device (B0BC) { Name (_ADR, 0x00170004) }
            Device (B0BD) { Name (_ADR, 0x00170005) }
            Device (B0BE) { Name (_ADR, 0x00170006) }
            Device (B0BF) { Name (_ADR, 0x00170007) }
            Device (B0C0) { Name (_ADR, 0x00180000) }
            Device (B0C1) { Name (_ADR, 0x00180001) }
            Device (B0C2) { Name (_ADR, 0x00180002) }
            Device (B0C3) { Name (_ADR, 0x00180003) }
            Device (B0C4) { Name (_ADR, 0x00180004) }
            Device (B0C5) { Name (_ADR, 0x00180005) }
            Device (B0C6) { Name (_ADR, 0x00180006) }
            Device (B0C7) { Name (_ADR, 0x00180007) }
            Device (B0C8) { Name (_ADR, 0x00190000) }
            Device (B0C9) { Name (_ADR, 0x00190001) }
            Device (B0CA) { Name (_ADR, 0x00190002) }
            Device (B0CB) { Name (_ADR, 0x00190003) }
            Device (B0CC) { Name (_ADR, 0x00190004) }
            Device (B0CD) { Name (_ADR, 0x00190005) }
            Device (B0CE) { Name (_ADR, 0x00190006) }
            Device (B0CF) { Name (_ADR, 0x00190007) }
            Device (B0D0) { Name (_ADR, 0x001A0000) }
            Device (B0D1) { Name (_ADR, 0x001A0001) }
            Device (B0D2) { Name (_ADR, 0x001A0002) }
            Device (B0D3) { Name (_ADR, 0x001A0003) }
            Device (B0D4) { Name (_ADR, 0x001A0004) }
            Device (B0D5) { Name (_ADR, 0x001A0005) }
            Device (B0D6) { Name (_ADR, 0x001A0006) }
            Device (B0D7) { Name (_ADR, 0x001A0007) }
            Device (B0D8) { Name (_ADR, 0x001B0000) }
            Device (B0D9) { Name (_ADR, 0x001B0001) }
            Device (B0DA) { Name (_ADR, 0x001B0002) }
            Device (B0DB) { Name (_ADR, 0x001B0003) }
            Device (B0DC) { Name (_ADR, 0x001B0004) }
            Device (B0DD) { Name (_ADR, 0x001B0005) }
            Device (B0DE) { Name (_ADR, 0x001B0006) }
            Device (B0DF) { Name (_ADR, 0x001B0007) }
            Device (B0E0) { Name (_ADR, 0x001C0000) }
            Device (B0E1) { Name (_ADR, 0x001C0001) }
            Device (B0E2) { Name (_ADR, 0x001C0002) }
            Device (B0E3) { Name (_ADR, 0x001C0003) }
            Device (B0E4) { Name (_ADR, 0x001C0004) }
            Device (B0E5) { Name (_ADR, 0x001C0005) }
            Device (B0E6) { Name (_ADR, 0x001C0006) }
            Device (B0E7) { Name (_ADR, 0x001C0007) }
            Device (B0E8) { Name (_ADR, 0x001D0000) }
            Device (B0E9) { Name (_ADR, 0x001D0001) }
            Device (B0EA) { Name (_ADR, 0x001D0002) }
            Device (B0EB) { Name (_ADR, 0x001D0003) }
            Device (B0EC) { Name (_ADR, 0x001D0004) }
            Device (B0ED) { Name (_ADR, 0x001D0005) }
            Device (B0EE) { Name (_ADR, 0x001D0006) }
            Device (B0EF) { Name (_ADR, 0x001D0007) }
            Device (B0F0) { Name (_ADR, 0x001E0000) }
            Device (B0F1) { Name (_ADR, 0x001E0001) }
            Device (B0F2) { Name (_ADR, 0x001E0002) }
            Device (B0F3) { Name (_ADR, 0x001E0003) }
            Device (B0F4) { Name (_ADR, 0x001E0004) }
            Device (B0F5) { Name (_ADR, 0x001E0005) }
            Device (B0F6) { Name (_ADR, 0x001E0006) }
            Device (B0F7) { Name (_ADR, 0x001E0007) }
            Device (B0F8) { Name (_ADR, 0x001F0000) }
            Device (B0F9) { Name (_ADR, 0x001F0001) }
            Device (B0FA) { Name (_ADR, 0x001F0002) }
            Device (B0FB) { Name (_ADR, 0x001F0003) }
            Device (B0FC) { Name (_ADR, 0x001F0004) }
            Device (B0FD) { Name (_ADR, 0x001F0005) }
            Device (B0FE) { Name (_ADR, 0x001F0006) }
            Device (B0FF) { Name (_ADR, 0x001F0007) }
            Device (B100) { Name (_ADR, Zero) }
        }
    }
}
