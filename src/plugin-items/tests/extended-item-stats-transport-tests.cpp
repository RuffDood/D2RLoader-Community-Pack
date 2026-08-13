#include "extended-item-stats-transport.h"

#include <algorithm>
#include <array>
#include "../../../tests/test-check.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace ruffneck::extended_item_stats;

namespace {
template<class Callable>
bool Throws(Callable&& callable) {
    try {
        callable();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
} // namespace

int main() {
    constexpr std::array<std::uint8_t, 9> crcSample{'1','2','3','4','5','6','7','8','9'};
    TEST_REQUIRE(Crc32(crcSample) == 0xCBF43926U);

    std::vector<std::uint8_t> item(271);
    for (std::size_t index = 0; index < item.size(); ++index) {
        item[index] = static_cast<std::uint8_t>((index * 73 + 19) & 0xFF);
    }
    const auto frames = FragmentItem(item, 0x45585431U);
    TEST_REQUIRE(frames.size() == 2);
    TEST_REQUIRE(frames[0].size() == 239);
    TEST_REQUIRE(frames[1].size() == 96);

    Reassembler receiver;
    constexpr std::array<std::uint8_t, 7> envelope{0x9D, 4, 1, 2, 3, 4, 5};
    auto pending = receiver.Accept(frames[1], "server", envelope, 1000);
    TEST_REQUIRE(pending.status == AcceptResult::Status::Pending);
    auto complete = receiver.Accept(frames[0], "server", envelope, 1001);
    TEST_REQUIRE(complete.status == AcceptResult::Status::Complete);
    TEST_REQUIRE(complete.itemBytes == item);
    TEST_REQUIRE(receiver.InFlightTransfers() == 0);

    TransportOptions bounded{};
    bounded.maxInFlightTransfers = 1;
    bounded.reassemblyTimeoutMs = 50;
    Reassembler boundedReceiver(bounded);
    const auto first = FragmentItem(std::vector<std::uint8_t>(300, 1), 1, bounded);
    const auto second = FragmentItem(std::vector<std::uint8_t>(300, 2), 2, bounded);
    boundedReceiver.Accept(first[0], "server", envelope, 100);
    TEST_REQUIRE(Throws([&] { boundedReceiver.Accept(second[0], "server", envelope, 120); }));
    TEST_REQUIRE(boundedReceiver.Expire(150) == 1);
    TEST_REQUIRE(boundedReceiver.Accept(second[0], "server", envelope, 151).status
        == AcceptResult::Status::Pending);

    Reassembler duplicateReceiver;
    duplicateReceiver.Accept(first[0], "server", envelope, 200);
    TEST_REQUIRE(Throws([&] { duplicateReceiver.Accept(first[0], "server", envelope, 201); }));
    TEST_REQUIRE(duplicateReceiver.InFlightTransfers() == 0);

    Reassembler envelopeReceiver;
    constexpr std::array<std::uint8_t, 7> otherEnvelope{0x9D, 5, 1, 2, 3, 4, 5};
    envelopeReceiver.Accept(first[0], "server", envelope, 200);
    TEST_REQUIRE(Throws([&] { envelopeReceiver.Accept(first[1], "server", otherEnvelope, 201); }));
    TEST_REQUIRE(envelopeReceiver.InFlightTransfers() == 0);

    TEST_REQUIRE(Throws([&] { FragmentItem(std::vector<std::uint8_t>(4097), 3); }));
    auto corrupted = frames;
    corrupted.back().back() ^= 0xFF;
    Reassembler corruptedReceiver;
    corruptedReceiver.Accept(corrupted[0], "server", envelope, 300);
    TEST_REQUIRE(Throws([&] { corruptedReceiver.Accept(corrupted[1], "server", envelope, 301); }));
    return 0;
}
