#include <kernel/net/arp.h>
#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
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
 * @brief An Ethernet header reads in place and writes back, and a frame is for the station or for all.
 */
KERNEL_TEST(an_ethernet_header_reads_where_it_lies_and_writes_back)
{
    static const uint8_t own[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
    static const uint8_t other[6] = {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
    static const uint8_t broadcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t frame[20] = {0u};
    KernelEthernetFrame_t parsed;

    kernel_ethernet_write_header(frame, own, other, KERNEL_ETHERNET_TYPE_ARP);
    kernel_test_check(test, kernel_ethernet_parse(frame, sizeof(frame), &parsed), "a frame of 20 bytes reads");
    kernel_test_check(test,
                      parsed.destination == frame && memcmp(parsed.source, other, 6u) == 0 && parsed.type == 0x0806u &&
                          parsed.payload == frame + 14 && parsed.payload_bytes == 6u,
                      "its header and payload read where they lie, EtherType 0806h big-endian");
    kernel_test_check(test, frame[12] == 0x08u && frame[13] == 0x06u, "the EtherType is written big-endian");
    kernel_test_check(test, kernel_ethernet_is_for(&parsed, own), "it is for the station it names");
    kernel_test_check(test, !kernel_ethernet_is_for(&parsed, other), "and not for another");
    kernel_ethernet_write_header(frame, broadcast, other, KERNEL_ETHERNET_TYPE_ARP);
    (void) kernel_ethernet_parse(frame, sizeof(frame), &parsed);
    kernel_test_check(test, kernel_ethernet_is_for(&parsed, own), "a broadcast frame is for every station");
    kernel_test_check(test, !kernel_ethernet_parse(frame, 13u, &parsed), "a frame of 13 bytes does not read");
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
}

/**
 * @brief The ARP cache learns, refreshes, forgets after its lifetime, and evicts its oldest entry when full.
 */
KERNEL_TEST(the_arp_cache_forgets_by_the_time_it_is_given)
{
    static KernelAddressResolutionCache_t cache;
    uint8_t station[6] = {1u, 2u, 3u, 4u, 5u, 6u};
    uint8_t found[6] = {0u};

    kernel_address_resolution_cache_initialize(&cache, 100u);
    kernel_address_resolution_cache_learn(&cache, 0x0A000001u, station, 10u);
    kernel_test_check(test, kernel_address_resolution_cache_find(&cache, 0x0A000001u, 109u, found) && found[5] == 6u,
                      "an address learned at 10 is found at 109");
    kernel_test_check(test, !kernel_address_resolution_cache_find(&cache, 0x0A000001u, 110u, found),
                      "and forgotten at 110, a lifetime of 100 later");
    kernel_address_resolution_cache_learn(&cache, 0x0A000001u, station, 150u);
    kernel_test_check(test, kernel_address_resolution_cache_find(&cache, 0x0A000001u, 200u, found),
                      "learned again at 150, it is found at 200");
    for (uint32_t entry = 1u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        kernel_address_resolution_cache_learn(&cache, 0x0A000100u + entry, station, 150u + entry);
    station[5] = 7u;
    kernel_address_resolution_cache_learn(&cache, 0x0A000200u, station, 180u);
    kernel_test_check(test, !kernel_address_resolution_cache_find(&cache, 0x0A000001u, 180u, found),
                      "a 17th address evicts the oldest");
    kernel_test_check(test, kernel_address_resolution_cache_find(&cache, 0x0A000200u, 180u, found) && found[5] == 7u,
                      "and is found itself");
    kernel_test_check(test, !kernel_address_resolution_cache_find(&cache, 0u, 180u, found), "address 0 is never found");
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

    uint8_t written[20 + 8] = {0u};

    kernel_internet_protocol_write_header(written, 0x0A00020Fu, 0x0A000202u, KERNEL_INTERNET_PROTOCOL_ICMP, 8u, 7u);
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
 * @brief An echo request is recognised and answered with its identifier, sequence and data, its checksum sound.
 */
KERNEL_TEST(an_echo_request_is_answered_with_its_own_data)
{
    uint8_t request[12] = {8u, 0u, 0u, 0u, 0x12u, 0x34u, 0x00u, 0x05u, 'p', 'i', 'n', 'g'};
    uint8_t reply[12] = {0u};

    kernel_network_write_16(request + 2u, network_stack_test_naive_checksum(request, 12u));
    kernel_test_check(test, kernel_internet_control_message_is_echo_request(request, 12u),
                      "a sound echo request is recognised");
    kernel_internet_control_message_write_echo_reply(reply, request, 12u);
    kernel_test_check(test, reply[0] == 0u && reply[1] == 0u && memcmp(reply + 4, request + 4, 8u) == 0,
                      "its reply is type 0 with the request's identifier, sequence and data");
    kernel_test_check(test, network_stack_test_naive_checksum(reply, 12u) == 0u,
                      "and its checksum holds by the test's sum");
    request[11] ^= 0x01u;
    kernel_test_check(test, !kernel_internet_control_message_is_echo_request(request, 12u),
                      "a request with a flipped bit is not");
}

/**
 * @brief A packet on the subnet goes to its destination's station, one off it to the gateway's.
 *
 * @details The card is made up: the lookup reads only the interface's addresses and its cache.
 */
KERNEL_TEST(a_packet_goes_to_its_own_station_on_the_subnet_and_to_the_gateway_off_it)
{
    static KernelNetworkQueue_t queue;
    static KernelNetworkDevice_t device = {.queue_count = 1u, .queues = &queue};
    static KernelNetworkInterface_t interface;
    static const uint8_t gateway_station[6] = {0x02u, 0u, 0u, 0u, 0u, 0x01u};
    static const uint8_t neighbour_station[6] = {0x02u, 0u, 0u, 0u, 0u, 0x02u};
    uint8_t found[6] = {0u};

    kernel_test_check(
        test, kernel_network_interface_initialize(&interface, &device, 0x0A000002u, 0xFFFFFF00u, 0x0A000001u, 100u),
        "an interface 10.0.0.2/24 through 10.0.0.1 sets up");
    kernel_address_resolution_cache_learn(&interface.cache, 0x0A000001u, gateway_station, 0u);
    kernel_address_resolution_cache_learn(&interface.cache, 0x0A000007u, neighbour_station, 0u);
    kernel_test_check(test, kernel_network_interface_find_station(&interface, 0x0A000007u, 1u, found) && found[5] == 2u,
                      "10.0.0.7 goes to its own station");
    kernel_test_check(test, kernel_network_interface_find_station(&interface, 0x08080808u, 1u, found) && found[5] == 1u,
                      "8.8.8.8 goes to the gateway's");
    kernel_test_check(test, !kernel_network_interface_find_station(&interface, 0x0A000009u, 1u, found),
                      "and 10.0.0.9, never learned, is not found");
    kernel_test_check(test, !kernel_network_interface_initialize(&interface, NULL, 0u, 0u, 0u, 1u),
                      "an interface on no card does not set up");
}
