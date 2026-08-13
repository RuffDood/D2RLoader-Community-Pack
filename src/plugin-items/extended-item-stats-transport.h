#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ruffneck::extended_item_stats {

inline constexpr std::uint8_t TransportVersion = 1;
inline constexpr std::size_t FrameHeaderBytes = 32;
inline constexpr std::size_t DefaultFrameBytes = 0xEF;
inline constexpr std::size_t DefaultMaxItemBytes = 0x1000;
inline constexpr std::size_t DefaultMaxInFlightTransfers = 32;
inline constexpr std::uint64_t DefaultReassemblyTimeoutMs = 5000;

struct TransportOptions {
    std::size_t frameBytes{DefaultFrameBytes};
    std::size_t maxItemBytes{DefaultMaxItemBytes};
    std::size_t maxInFlightTransfers{DefaultMaxInFlightTransfers};
    std::uint64_t reassemblyTimeoutMs{DefaultReassemblyTimeoutMs};
};

struct FrameInfo {
    std::uint32_t transferId{};
    std::uint32_t totalItemBytes{};
    std::uint32_t itemChecksum{};
    std::uint32_t chunkOffset{};
    std::uint16_t chunkIndex{};
    std::uint16_t chunkCount{};
    std::span<const std::uint8_t> payload{};
};

struct AcceptResult {
    enum class Status { Pending, Complete } status{Status::Pending};
    std::uint32_t transferId{};
    std::size_t receivedChunks{};
    std::size_t chunkCount{};
    std::vector<std::uint8_t> itemBytes{};
};

std::uint32_t Crc32(std::span<const std::uint8_t> bytes) noexcept;
std::vector<std::vector<std::uint8_t>> FragmentItem(
    std::span<const std::uint8_t> itemBytes,
    std::uint32_t transferId,
    const TransportOptions& options = {});
FrameInfo ParseFrame(
    std::span<const std::uint8_t> frame,
    const TransportOptions& options = {});

class Reassembler {
public:
    explicit Reassembler(TransportOptions options = {});

    AcceptResult Accept(
        std::span<const std::uint8_t> frame,
        std::string_view channelKey,
        std::span<const std::uint8_t> envelopeIdentity,
        std::uint64_t nowMs);
    std::size_t Expire(std::uint64_t nowMs) noexcept;
    [[nodiscard]] std::size_t InFlightTransfers() const noexcept;
    void Clear() noexcept;

private:
    struct Impl;
    TransportOptions options_;
    Impl* impl_{};

public:
    Reassembler(const Reassembler&) = delete;
    Reassembler& operator=(const Reassembler&) = delete;
    Reassembler(Reassembler&&) noexcept;
    Reassembler& operator=(Reassembler&&) noexcept;
    ~Reassembler();
};

} // namespace ruffneck::extended_item_stats
