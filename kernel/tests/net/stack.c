#include <kernel/net/arp.h>
#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/udp.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

/**
 * @brief The Internet checksum of @p length bytes, summed the slow way, apart from the stack's own sum.
 */
static uint16_t network_stack_test_naive_checksum(const uint8_t *bytes, uint32_t length)
{
    uint32_t sum = 0u;

    for (uint32_t byte = 0u; byte < length; ++byte)
        sum += (byte % 2u == 0u) ? (uint32_t) bytes[byte] << 8 : (uint32_t) bytes[byte];
    while (sum > 0xFFFFu)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t) ~sum;
}

KERNEL_TEST_SUITE(network_stack, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The checksum of RFC 1071's example, summed whole or in two parts, is 220Dh.
 */
KERNEL_TEST(the_checksum_of_the_rfc_example_is_its_published_value)
{
    static const uint8_t words[8] = {0x00u, 0x01u, 0xF2u, 0x03u, 0xF4u, 0xF5u, 0xF6u, 0xF7u};
    static const uint8_t odd[3] = {0xABu, 0xCDu, 0xEFu};
    static const uint8_t carries[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x00u, 0x01u};

    kernel_test_check(test, kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, words, 8u)) == 0x220Du,
                      "the words 0001h F203h F4F5h F6F7h sum to the checksum 220Dh (RFC 1071, 3)");
    kernel_test_check(test,
                      kernel_internet_checksum_finish(kernel_internet_checksum_add(
                          kernel_internet_checksum_add(0u, words, 4u), words + 4u, 4u)) == 0x220Du,
                      "and so they do summed in two parts");
    kernel_test_check(test,
                      kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, odd, 3u)) ==
                          network_stack_test_naive_checksum(odd, 3u),
                      "an odd last byte counts as a high byte");
    kernel_test_check(test,
                      kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, carries, 6u)) == 0xFFFEu &&
                          network_stack_test_naive_checksum(carries, 6u) == 0xFFFEu,
                      "FFFFh, FFFFh and 0001h fold twice, to the checksum FFFEh");
}

/**
 * @brief The folded sum, four bytes a load in the host's order, gives the checksum the exact sum gives, at every
 *        length from 0 to 40 and every alignment from 0 to 3, and folds bytes all zero to zero.
 */
KERNEL_TEST(the_folded_sum_gives_the_checksum_the_exact_sum_gives)
{
    static uint8_t bytes[48];
    static const uint8_t zeros[9] = {0u};
    static const uint8_t carries[12] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x01u, 0u, 0u, 0u};
    uint32_t disagreements = 0u;

    for (uint32_t byte = 0u; byte < sizeof(bytes); ++byte)
        bytes[byte] = (uint8_t) (byte * 37u + 11u);
    bytes[17] = 0xFFu;
    bytes[18] = 0xFFu;
    for (uint32_t offset = 0u; offset < 4u; ++offset)
        for (uint32_t length = 0u; length <= 40u; ++length)
            disagreements +=
                kernel_internet_checksum_finish(kernel_internet_checksum_fold_bytes(bytes + offset, length)) !=
                        network_stack_test_naive_checksum(bytes + offset, length) ?
                    1u :
                    0u;
    kernel_test_check(test, disagreements == 0u, "164 lengths and alignments, each checksum the test's own");
    kernel_test_check(test,
                      kernel_internet_checksum_finish(kernel_internet_checksum_fold_bytes(carries, sizeof(carries))) ==
                          network_stack_test_naive_checksum(carries, sizeof(carries)),
                      "two words of FFFFFFFFh and a 1, whose 64-bit sum carries twice, fold as the test's sum does");
    kernel_test_check(test,
                      kernel_internet_checksum_fold_bytes(zeros, 9u) == 0u &&
                          kernel_internet_checksum_fold_bytes(bytes, 0u) == 0u,
                      "bytes all zero, or none, fold to zero");
}

/**
 * @brief An Ethernet header reads in place and writes back, and a frame is for the station or for all.
 */
KERNEL_TEST(an_ethernet_header_reads_where_it_lies_and_writes_back)
{
    static const uint8_t own[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
    static const uint8_t other[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
    static const uint8_t broadcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t frame[20] = {0u};
    KernelEthernetFrame_t parsed;

    kernel_ethernet_write_header(frame, own, other, KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION);
    kernel_test_check(test, kernel_ethernet_parse(frame, sizeof(frame), &parsed), "a frame of 20 bytes reads");
    kernel_test_check(test,
                      parsed.destination == frame && memcmp(parsed.source, other, 6u) == 0 && parsed.type == 0x0806u &&
                          parsed.payload == frame + 14 && parsed.payload_bytes == 6u,
                      "its header and payload read where they lie, EtherType 0806h big-endian");
    kernel_test_check(test, frame[12] == 0x08u && frame[13] == 0x06u, "the EtherType is written big-endian");
    kernel_test_check(test, kernel_ethernet_is_for(&parsed, own), "it is for the station it names");
    kernel_test_check(test, !kernel_ethernet_is_for(&parsed, other), "and not for another");
    kernel_ethernet_write_header(frame, broadcast, other, KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION);
    (void) kernel_ethernet_parse(frame, sizeof(frame), &parsed);
    kernel_test_check(test, kernel_ethernet_is_for(&parsed, own), "a broadcast frame is for every station");
    kernel_test_check(test, !kernel_ethernet_parse(frame, 13u, &parsed), "a frame of 13 bytes does not read");
    kernel_test_check(test, kernel_ethernet_is_group(broadcast) && !kernel_ethernet_is_group(own),
                      "the broadcast address is a group, the station's own is not");
}

/**
 * @brief An ARP request for 10.0.2.2 from 10.0.2.15 reads as RFC 826 lays it out, and writes back the same bytes.
 */
KERNEL_TEST(an_arp_packet_reads_as_rfc_826_lays_it_out)
{
    static const uint8_t request[28] = {0x00u, 0x01u, 0x08u, 0x00u, 0x06u, 0x04u, 0x00u, 0x01u, 0x52u, 0x54u,
                                        0x00u, 0x12u, 0x34u, 0x56u, 10u,   0u,    2u,    15u,   0x00u, 0x00u,
                                        0x00u, 0x00u, 0x00u, 0x00u, 10u,   0u,    2u,    2u};
    uint8_t written[28] = {0u};
    uint8_t wrong[28];
    KernelAddressResolutionPacket_t packet;

    kernel_test_check(test, kernel_address_resolution_parse(request, 28u, &packet), "the request reads");
    kernel_test_check(test,
                      packet.operation == KERNEL_ADDRESS_RESOLUTION_REQUEST && packet.sender_address == 0x0A00020Fu &&
                          packet.target_address == 0x0A000202u && packet.sender_station[5] == 0x56u,
                      "operation 1, from 10.0.2.15 at 52:54:00:12:34:56, for 10.0.2.2");
    kernel_address_resolution_write(written, &packet);
    kernel_test_check(test, memcmp(written, request, 28u) == 0, "and writes back the same 28 bytes");
    memcpy(wrong, request, 28u);
    wrong[5] = 16u;
    kernel_test_check(test, !kernel_address_resolution_parse(wrong, 28u, &packet),
                      "a protocol address of 16 bytes does not");
    kernel_test_check(test, !kernel_address_resolution_parse(request, 27u, &packet), "nor does a packet of 27 bytes");
    memcpy(wrong, request, 28u);
    wrong[1] = 6u;
    kernel_test_check(test, !kernel_address_resolution_parse(wrong, 28u, &packet), "nor hardware 6, IEEE 802");
    memcpy(wrong, request, 28u);
    wrong[4] = 8u;
    kernel_test_check(test, !kernel_address_resolution_parse(wrong, 28u, &packet), "nor station addresses of 8 bytes");
    memcpy(wrong, request, 28u);
    wrong[2] = 0x86u;
    wrong[3] = 0xDDu;
    kernel_test_check(test, !kernel_address_resolution_parse(wrong, 28u, &packet), "nor protocol 86DDh, IPv6");
}

/**
 * @brief The ARP cache learns, refreshes by merge, forgets after its lifetime, and lets a station that only talked to
 *        the interface evict none it asked for.
 */
KERNEL_TEST(the_arp_cache_forgets_by_the_time_it_is_given)
{
    static KernelAddressResolutionCache_t cache;
    uint8_t station[6] = {1u, 2u, 3u, 4u, 5u, 6u};
    uint8_t found[6] = {0u};

    kernel_address_resolution_cache_initialize(&cache, 100u);
    kernel_test_check(test, kernel_address_resolution_cache_add(&cache, 0x0A000001u, station, 10u),
                      "an address is added at 10");
    kernel_test_check(test, kernel_address_resolution_cache_find(&cache, 0x0A000001u, 109u, found) && found[5] == 6u,
                      "and found at 109");
    kernel_test_check(test, !kernel_address_resolution_cache_find(&cache, 0x0A000001u, 110u, found),
                      "and forgotten at 110, a lifetime of 100 later");
    station[5] = 7u;
    kernel_test_check(test,
                      kernel_address_resolution_cache_merge(&cache, 0x0A000001u, station, 150u) &&
                          kernel_address_resolution_cache_find(&cache, 0x0A000001u, 200u, found) && found[5] == 7u,
                      "merged at 150 with a new station, it is found at 200 with it");
    kernel_test_check(test, !kernel_address_resolution_cache_merge(&cache, 0x0A000002u, station, 150u),
                      "an address the cache does not hold is not merged");
    kernel_test_check(test, !kernel_address_resolution_cache_find(&cache, 0u, 180u, found), "address 0 is never found");
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        kernel_address_resolution_cache_expire_next(&cache, 250u);
    kernel_test_check(
        test, !kernel_address_resolution_cache_find(&cache, 0x0A000001u, 150u, found),
        "16 expiries at 250, one an entry, free it: at 150 it is gone too, as the time comes back around");
}

/**
 * @brief An address asked for is held unresolved until a merge resolves it, and a full cache evicts its oldest entry
 *        not asked for, never one asked for, to add a station.
 */
KERNEL_TEST(the_arp_cache_keeps_the_addresses_asked_for)
{
    static KernelAddressResolutionCache_t cache;
    uint8_t station[6] = {2u, 0u, 0u, 0u, 0u, 1u};
    uint8_t found[6] = {0u};

    kernel_address_resolution_cache_initialize(&cache, 100u);
    kernel_address_resolution_cache_ask(&cache, 0x0A0000FEu, 0u);
    kernel_test_check(test, !kernel_address_resolution_cache_find(&cache, 0x0A0000FEu, 1u, found),
                      "an address asked for is not found before its reply");
    kernel_test_check(test,
                      kernel_address_resolution_cache_merge(&cache, 0x0A0000FEu, station, 1u) &&
                          kernel_address_resolution_cache_find(&cache, 0x0A0000FEu, 2u, found),
                      "and is once its reply merged");
    (void) kernel_address_resolution_cache_add(&cache, 0x0A000101u, station, 2u);
    (void) kernel_address_resolution_cache_add(&cache, 0x0A000102u, station, 3u);
    (void) kernel_address_resolution_cache_merge(&cache, 0x0A000101u, station, 4u);
    for (uint32_t entry = 3u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        (void) kernel_address_resolution_cache_add(&cache, 0x0A000100u + entry, station, 10u + entry);
    kernel_test_check(test, kernel_address_resolution_cache_add(&cache, 0x0A000200u, station, 50u),
                      "a 17th address is added to the full cache");
    kernel_test_check(test,
                      !kernel_address_resolution_cache_find(&cache, 0x0A000102u, 50u, found) &&
                          kernel_address_resolution_cache_find(&cache, 0x0A000101u, 50u, found) &&
                          kernel_address_resolution_cache_find(&cache, 0x0A000200u, 50u, found),
                      "in the place of the oldest not asked for, learned at 3, not the first, refreshed at 4");
    kernel_test_check(test, kernel_address_resolution_cache_find(&cache, 0x0A0000FEu, 50u, found),
                      "and the address asked for, learned at 1, the oldest of all, stays");
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        kernel_address_resolution_cache_ask(&cache, 0x0A000300u + entry, 60u + entry);
    kernel_test_check(test, !kernel_address_resolution_cache_add(&cache, 0x0A000400u, station, 80u),
                      "a cache of 16 addresses asked for takes no station that only talked");
    kernel_address_resolution_cache_ask(&cache, 0x0A000500u, 90u);
    kernel_test_check(test,
                      kernel_address_resolution_cache_merge(&cache, 0x0A000500u, station, 91u) &&
                          !kernel_address_resolution_cache_merge(&cache, 0x0A000300u, station, 91u),
                      "but an address asked for takes the place of the oldest asked for");
}

/**
 * @brief A hinted lookup finds what a plain one finds, from a hint right or wrong, and leaves the entry found in its
 *        hint.
 */
KERNEL_TEST(a_hinted_lookup_finds_what_a_plain_one_finds)
{
    static KernelAddressResolutionCache_t cache;
    uint8_t station[6] = {2u, 0u, 0u, 0u, 0u, 9u};
    uint8_t found[6] = {0u};
    uint32_t hint = 0u;

    kernel_address_resolution_cache_initialize(&cache, 100u);
    for (uint32_t entry = 0u; entry < 5u; ++entry)
        (void) kernel_address_resolution_cache_add(&cache, 0x0A000010u + entry, station, entry);
    station[5] = 4u;
    (void) kernel_address_resolution_cache_merge(&cache, 0x0A000014u, station, 5u);
    kernel_test_check(test,
                      kernel_address_resolution_cache_find_hinted(&cache, 0x0A000014u, 6u, found, &hint) &&
                          found[5] == 4u && hint == 4u,
                      "from a wrong hint, 10.0.0.20 is found in entry 4, which the hint then names");
    found[5] = 0u;
    kernel_test_check(
        test, kernel_address_resolution_cache_find_hinted(&cache, 0x0A000014u, 6u, found, &hint) && found[5] == 4u,
        "and is found again from it");
    hint = 77u;
    kernel_test_check(test,
                      !kernel_address_resolution_cache_find_hinted(&cache, 0x0A000099u, 6u, found, &hint) &&
                          hint == 77u && !kernel_address_resolution_cache_find_hinted(&cache, 0u, 6u, found, &hint),
                      "an address not held, or 0, is not found, the hint left as it was");
    kernel_address_resolution_cache_ask(&cache, 0x0A000030u, 7u);
    hint = 5u;
    kernel_test_check(test,
                      !kernel_address_resolution_cache_find_hinted(&cache, 0x0A000030u, 8u, found, &hint) &&
                          !kernel_address_resolution_cache_find_hinted(&cache, 0x0A000014u, 105u, found, &hint),
                      "nor is an address asked for and unresolved, nor one a lifetime old");
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        kernel_address_resolution_cache_expire_next(&cache, 200u);
    hint = 4u;
    kernel_test_check(test,
                      !kernel_address_resolution_cache_find(&cache, 0x0A000014u, 5u, found) &&
                          !kernel_address_resolution_cache_find_hinted(&cache, 0u, 201u, found, &hint),
                      "16 expiries free every entry, the fifth one too: at 5 again, 10.0.0.20 is gone, and address 0 "
                      "is not found where it was");
}

/**
 * @brief Asking again for an expired address starts it over unresolved, so that the expiry of a batch does not
 *        free it before its reply.
 */
KERNEL_TEST(asking_again_for_an_expired_address_starts_it_over)
{
    static KernelAddressResolutionCache_t cache;
    uint8_t station[6] = {2u, 0u, 0u, 0u, 0u, 1u};
    uint8_t found[6] = {0u};

    kernel_address_resolution_cache_initialize(&cache, 100u);
    (void) kernel_address_resolution_cache_add(&cache, 0x0A000001u, station, 10u);
    kernel_address_resolution_cache_ask(&cache, 0x0A000001u, 50u);
    kernel_test_check(test, kernel_address_resolution_cache_find(&cache, 0x0A000001u, 60u, found),
                      "asked for while alive, it keeps its station");
    kernel_address_resolution_cache_ask(&cache, 0x0A000001u, 200u);
    kernel_test_check(test, !kernel_address_resolution_cache_find(&cache, 0x0A000001u, 200u, found),
                      "asked for again once expired, it is unresolved");
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        kernel_address_resolution_cache_expire_next(&cache, 250u);
    kernel_test_check(test,
                      kernel_address_resolution_cache_merge(&cache, 0x0A000001u, station, 260u) &&
                          kernel_address_resolution_cache_find(&cache, 0x0A000001u, 261u, found),
                      "and survives 16 expiries at 250 to take its reply at 260");
}

/**
 * @brief A real IPv4 header, checksum B861h, is taken; each kind of unsound one is refused by its cause.
 *
 * @details The header is the one of a UDP packet from 192.168.0.1 to 192.168.0.199 often printed with its
 *          checksum, and its checksum is checked again here by the test's own sum.
 */
KERNEL_TEST(an_ipv4_header_is_taken_or_refused_by_its_cause)
{
    static uint8_t packet[0x73];
    static const uint8_t header[20] = {0x45u, 0x00u, 0x00u, 0x73u, 0x00u, 0x00u, 0x40u, 0x00u, 0x40u, 0x11u,
                                       0xB8u, 0x61u, 0xC0u, 0xA8u, 0x00u, 0x01u, 0xC0u, 0xA8u, 0x00u, 0xC7u};
    KernelInternetProtocolPacket_t parsed;

    memcpy(packet, header, sizeof(header));
    kernel_test_check(test, network_stack_test_naive_checksum(packet, 20u) == 0u,
                      "the header's checksum holds by the test's sum");
    kernel_test_check(test,
                      kernel_internet_protocol_parse(packet, sizeof(packet), &parsed) == KERNEL_INTERNET_PROTOCOL_TAKEN,
                      "it is taken");
    kernel_test_check(test,
                      parsed.source == 0xC0A80001u && parsed.destination == 0xC0A800C7u && parsed.protocol == 17u &&
                          parsed.payload == packet + 20 && parsed.payload_bytes == 0x73u - 20u,
                      "from 192.168.0.1 to 192.168.0.199, UDP, 95 bytes of payload");
    kernel_test_check(test,
                      kernel_internet_protocol_parse(packet, 0x72u, &parsed) == KERNEL_INTERNET_PROTOCOL_TRUNCATED,
                      "a byte short of its total length, it is truncated");
    packet[10] ^= 0x01u;
    kernel_test_check(
        test, kernel_internet_protocol_parse(packet, sizeof(packet), &parsed) == KERNEL_INTERNET_PROTOCOL_BAD_CHECKSUM,
        "a flipped checksum bit is refused");
    packet[10] ^= 0x01u;
    packet[0] = 0x65u;
    kernel_test_check(
        test, kernel_internet_protocol_parse(packet, sizeof(packet), &parsed) == KERNEL_INTERNET_PROTOCOL_NOT_VERSION_4,
        "version 6 is refused");
    packet[0] = 0x44u;
    kernel_test_check(test,
                      kernel_internet_protocol_parse(packet, sizeof(packet), &parsed) ==
                          KERNEL_INTERNET_PROTOCOL_BAD_HEADER_LENGTH,
                      "an IHL of 4 is refused");

    packet[0] = 0x45u;
    packet[2] = 0x00u;
    packet[3] = 19u;
    kernel_test_check(test,
                      kernel_internet_protocol_parse(packet, sizeof(packet), &parsed) ==
                          KERNEL_INTERNET_PROTOCOL_BAD_TOTAL_LENGTH,
                      "a total length of 19 under its 20 of header is refused as such");
    packet[0] = 0x4Fu;
    packet[3] = 0x73u;
    kernel_test_check(test, kernel_internet_protocol_parse(packet, 40u, &parsed) == KERNEL_INTERNET_PROTOCOL_TRUNCATED,
                      "an IHL of 60 bytes in 40 is truncated");
    memcpy(packet, header, sizeof(header));
    packet[6] = 0x00u;
    packet[7] = 0x10u;
    kernel_network_write_16(packet + 10u, 0u);
    kernel_network_write_16(packet + 10u, network_stack_test_naive_checksum(packet, 20u));
    kernel_test_check(
        test, kernel_internet_protocol_parse(packet, sizeof(packet), &parsed) == KERNEL_INTERNET_PROTOCOL_FRAGMENT,
        "a last fragment, MF clear at offset 128, its checksum sound, is refused as a fragment");

    uint8_t written[20 + 8] = {0u};

    kernel_internet_protocol_write_header(written, 0x0A00020Fu, 0x0A000202u, KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE,
                                          8u, 7u);
    kernel_test_check(test,
                      network_stack_test_naive_checksum(written, 20u) == 0u && written[0] == 0x45u && written[9] == 1u,
                      "a header written has a checksum that holds by the test's sum");
    kernel_test_check(
        test, kernel_internet_protocol_parse(written, sizeof(written), &parsed) == KERNEL_INTERNET_PROTOCOL_TAKEN,
        "and is taken back");
    written[6] |= 0x20u;
    kernel_network_write_16(written + 10u, 0u);
    kernel_network_write_16(written + 10u, network_stack_test_naive_checksum(written, 20u));
    kernel_test_check(
        test, kernel_internet_protocol_parse(written, sizeof(written), &parsed) == KERNEL_INTERNET_PROTOCOL_FRAGMENT,
        "a first fragment, its checksum sound, is refused as a fragment");
}

/**
 * @brief An echo request is recognised and answered with its identifier, sequence and data, its checksum the one a
 *        sum from scratch gives, a message of zeros and one of odd length included.
 */
KERNEL_TEST(an_echo_request_is_answered_with_its_own_data)
{
    uint8_t request[12] = {8u, 0u, 0u, 0u, 0x12u, 0x34u, 0x00u, 0x05u, 'p', 'i', 'n', 'g'};
    uint8_t zeros[8] = {8u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    uint8_t odd[13] = {8u, 0u, 0u, 0u, 0xABu, 0xCDu, 0u, 1u, 1u, 2u, 3u, 4u, 5u};
    uint8_t reply[13] = {0u};
    KernelInternetControlMessage_t message;

    kernel_network_write_16(request + 2u, network_stack_test_naive_checksum(request, 12u));
    kernel_test_check(test,
                      kernel_internet_control_message_parse(request, 12u, &message) &&
                          kernel_internet_control_message_is_echo_request(&message),
                      "a sound echo request is recognised");
    kernel_internet_control_message_write_echo_reply(reply, &message);
    kernel_test_check(test, reply[0] == 0u && reply[1] == 0u && memcmp(reply + 4, request + 4, 8u) == 0,
                      "its reply is type 0 with the request's identifier, sequence and data");
    kernel_test_check(test, network_stack_test_naive_checksum(reply, 12u) == 0u,
                      "and its checksum holds by the test's sum");
    kernel_network_write_16(zeros + 2u, network_stack_test_naive_checksum(zeros, 8u));
    (void) kernel_internet_control_message_parse(zeros, 8u, &message);
    kernel_internet_control_message_write_echo_reply(reply, &message);
    kernel_test_check(test,
                      reply[2] == 0xFFu && reply[3] == 0xFFu && network_stack_test_naive_checksum(reply, 8u) == 0u,
                      "the reply of zeros carries FFFFh, as from scratch, not RFC 1624's 0000h");
    kernel_network_write_16(odd + 2u, network_stack_test_naive_checksum(odd, 13u));
    (void) kernel_internet_control_message_parse(odd, 13u, &message);
    kernel_internet_control_message_write_echo_reply(reply, &message);
    kernel_test_check(test, network_stack_test_naive_checksum(reply, 13u) == 0u, "and a reply of 13 bytes holds too");
    request[11] ^= 0x01u;
    kernel_test_check(test, !kernel_internet_control_message_parse(request, 12u, &message),
                      "a request with a flipped bit is refused");
    kernel_test_check(test, !kernel_internet_control_message_parse(zeros, 7u, &message),
                      "and so is a message of 7 bytes");
}

/**
 * @brief A datagram is taken from a source a host may send from, and refused from one RFC 1122 (3.2.1.3) rules out.
 */
KERNEL_TEST(a_source_no_host_sends_from_is_refused)
{
    static const uint32_t refused[] = {0u,          0x00010203u, 0x7F000001u, 0xE0000001u, 0xF0000001u,
                                       0xFFFFFFFFu, 0x0A0000FFu, 0x0A000000u, 0x0A000002u};
    bool every_one_refused = true;

    for (uint32_t source = 0u; source < sizeof(refused) / sizeof(refused[0]); ++source)
        every_one_refused = every_one_refused &&
                            !kernel_internet_protocol_source_is_acceptable(refused[source], 0x0A000002u, 0xFFFFFF00u);
    kernel_test_check(test,
                      kernel_internet_protocol_source_is_acceptable(0x0A000001u, 0x0A000002u, 0xFFFFFF00u) &&
                          kernel_internet_protocol_source_is_acceptable(0x08080808u, 0x0A000002u, 0xFFFFFF00u) &&
                          kernel_internet_protocol_source_is_acceptable(0x0A0001FFu, 0x0A000002u, 0xFFFFFF00u),
                      "10.0.0.1, 8.8.8.8 and 10.0.1.255, another subnet's, may send to 10.0.0.2/24");
    kernel_test_check(test, every_one_refused,
                      "0.0.0.0, 0.1.2.3, 127.0.0.1, 224.0.0.1, 240.0.0.1, 255.255.255.255, 10.0.0.255, 10.0.0.0 and "
                      "10.0.0.2 may not");
    kernel_test_check(test, kernel_internet_protocol_source_is_acceptable(0x0A000000u, 0x0A000001u, 0xFFFFFFFEu),
                      "on a /31, which has no broadcast, 10.0.0.0 may send to 10.0.0.1");
}

/**
 * @brief The checksum of a UDP datagram of @p datagram_bytes from @p source to @p destination, summed the slow way
 *        over a pseudo-header the test lays out itself, with the datagram's checksum field as it stands.
 */
static uint16_t network_stack_test_naive_datagram_checksum(uint32_t source, uint32_t destination,
                                                           const uint8_t *datagram, uint32_t datagram_bytes)
{
    static uint8_t whole[12u + 64u];

    kernel_network_write_32(whole, source);
    kernel_network_write_32(whole + 4u, destination);
    whole[8] = 0u;
    whole[9] = KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM;
    kernel_network_write_16(whole + 10u, (uint16_t) datagram_bytes);
    memcpy(whole + 12u, datagram, datagram_bytes);
    return network_stack_test_naive_checksum(whole, 12u + datagram_bytes);
}

/**
 * @brief A datagram's checksum covers its pseudo-header, header and payload, the payload summed once by the caller;
 *        one that folds to zero is sent as FFFFh (RFC 768).
 */
KERNEL_TEST(a_datagram_checksum_holds_by_the_tests_own_sum)
{
    uint8_t datagram[8u + 5u] = {0u};
    const uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};
    const uint32_t source = 0x0A000001u;
    const uint32_t destination = 0x0A000002u;
    const uint32_t payload_sum = kernel_internet_checksum_add(0u, payload, 5u);

    memcpy(datagram + 8u, payload, 5u);
    kernel_user_datagram_write_header(
        datagram, 7777u, 9000u, 5u,
        kernel_user_datagram_checksum(kernel_user_datagram_pseudo_header_sum(source, destination, 13u), 7777u, 9000u,
                                      5u, payload_sum));
    kernel_test_check(test, kernel_network_read_16(datagram + 4u) == 13u, "its length counts its header");
    kernel_test_check(test, network_stack_test_naive_datagram_checksum(source, destination, datagram, 13u) == 0u,
                      "and its checksum, from the payload's sum, holds by the test's own sum of the whole");

    uint8_t zero[8u + 2u] = {0u};
    uint32_t sum = 0u;

    kernel_user_datagram_write_header(zero, 1u, 2u, 2u, 0u);
    sum =
        (source >> 16) + (source & 0xFFFFu) + (destination >> 16) + (destination & 0xFFFFu) + 17u + 10u + 1u + 2u + 10u;
    while (sum > 0xFFFFu)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    kernel_network_write_16(zero + 8u, (uint16_t) (0xFFFFu - sum));
    kernel_test_check(test,
                      kernel_user_datagram_checksum(kernel_user_datagram_pseudo_header_sum(source, destination, 10u),
                                                    1u, 2u, 2u,
                                                    kernel_internet_checksum_add(0u, zero + 8u, 2u)) == 0xFFFFu,
                      "a datagram whose sum folds to FFFFh, a checksum of zero, carries FFFFh");
}

/**
 * @brief A datagram is read where it lies, its padding left out, and refused by its cause: too short, a length
 *        past its packet or under its header, a checksum that fails; a zero checksum is said apart.
 */
KERNEL_TEST(a_datagram_is_taken_or_refused_by_its_cause)
{
    uint8_t datagram[8u + 6u + 4u];
    const uint8_t payload[6] = {1u, 2u, 3u, 4u, 5u, 6u};
    KernelInternetProtocolPacket_t packet = {.source = 0x0A000001u,
                                             .destination = 0x0A000002u,
                                             .protocol = KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM,
                                             .payload = datagram,
                                             .payload_bytes = sizeof(datagram)};
    KernelUserDatagram_t read;

    memset(datagram, 0xEE, sizeof(datagram));
    memcpy(datagram + 8u, payload, 6u);
    kernel_user_datagram_write_header(datagram, 7777u, 9000u, 6u, 0u);
    kernel_network_write_16(
        datagram + 6u, network_stack_test_naive_datagram_checksum(packet.source, packet.destination, datagram, 14u));
    kernel_test_check(test,
                      kernel_user_datagram_parse(&packet, &read) == KERNEL_USER_DATAGRAM_TAKEN &&
                          read.source_port == 7777u && read.destination_port == 9000u && read.payload == datagram + 8 &&
                          read.payload_bytes == 6u,
                      "a datagram of 14 bytes in a payload of 18 is taken, its 4 bytes of padding left out");
    kernel_test_check(test, read.payload_sum == 0x0102u + 0x0304u + 0x0506u,
                      "its payload's sum, folded to 16 bits, carries none here");
    packet.payload_bytes = 7u;
    kernel_test_check(test, kernel_user_datagram_parse(&packet, &read) == KERNEL_USER_DATAGRAM_TRUNCATED,
                      "7 bytes are too short for a header");
    packet.payload_bytes = 13u;
    kernel_test_check(test, kernel_user_datagram_parse(&packet, &read) == KERNEL_USER_DATAGRAM_BAD_LENGTH,
                      "a length of 14 in 13 bytes is refused");
    packet.payload_bytes = sizeof(datagram);
    kernel_network_write_16(datagram + 4u, 7u);
    kernel_test_check(test, kernel_user_datagram_parse(&packet, &read) == KERNEL_USER_DATAGRAM_BAD_LENGTH,
                      "and so is a length of 7");
    kernel_network_write_16(datagram + 4u, 14u);
    datagram[13] ^= 0x01u;
    kernel_test_check(test, kernel_user_datagram_parse(&packet, &read) == KERNEL_USER_DATAGRAM_BAD_CHECKSUM,
                      "a flipped payload bit fails the checksum");
    kernel_network_write_16(datagram + 6u, 0u);
    kernel_test_check(test,
                      kernel_user_datagram_parse(&packet, &read) == KERNEL_USER_DATAGRAM_NO_CHECKSUM &&
                          read.payload_bytes == 6u,
                      "a zero checksum is said apart, the datagram read but unchecked");
}
