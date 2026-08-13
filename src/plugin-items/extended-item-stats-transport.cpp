#include "extended-item-stats-transport.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace ruffneck::extended_item_stats {
namespace {
constexpr std::array<std::uint8_t, 4> Magic{'E', 'I', 'T', '1'};
constexpr std::uint8_t FirstFlag = 0x01;
constexpr std::uint8_t LastFlag = 0x02;

void ValidateOptions(const TransportOptions& options) {
    if (options.frameBytes <= FrameHeaderBytes
        || options.frameBytes > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("frameBytes is outside the supported range");
    }
    if (options.maxItemBytes == 0
        || options.maxItemBytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("maxItemBytes is outside the supported range");
    }
    if (options.maxInFlightTransfers == 0
        || options.maxInFlightTransfers > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("maxInFlightTransfers is outside the supported range");
    }
    if (options.reassemblyTimeoutMs == 0) {
        throw std::invalid_argument("reassemblyTimeoutMs must be positive");
    }
}

void Write16(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void Write32(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

std::uint16_t Read16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
    }
    return value;
}

std::uint8_t ExpectedFlags(std::uint16_t chunkIndex, std::uint16_t chunkCount) {
    return static_cast<std::uint8_t>(
        (chunkIndex == 0 ? FirstFlag : 0)
        | (chunkIndex + 1 == chunkCount ? LastFlag : 0));
}

std::string TransferKey(std::string_view channelKey, std::uint32_t transferId) {
    if (channelKey.empty()) throw std::invalid_argument("channelKey must not be empty");
    std::string key(channelKey);
    key.push_back('\0');
    key.append(reinterpret_cast<const char*>(&transferId), sizeof(transferId));
    return key;
}
} // namespace

std::uint32_t Crc32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(value & 1U));
            value = (value >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return value ^ 0xFFFFFFFFU;
}

std::vector<std::vector<std::uint8_t>> FragmentItem(
    std::span<const std::uint8_t> itemBytes,
    std::uint32_t transferId,
    const TransportOptions& options) {
    ValidateOptions(options);
    if (itemBytes.empty()) throw std::invalid_argument("itemBytes must not be empty");
    if (itemBytes.size() > options.maxItemBytes) {
        throw std::length_error("item exceeds maxItemBytes");
    }

    const auto framePayloadBytes = options.frameBytes - FrameHeaderBytes;
    const auto chunkCountValue = (itemBytes.size() + framePayloadBytes - 1) / framePayloadBytes;
    if (chunkCountValue > std::numeric_limits<std::uint16_t>::max()) {
        throw std::length_error("item requires too many chunks");
    }
    const auto chunkCount = static_cast<std::uint16_t>(chunkCountValue);
    const auto checksum = Crc32(itemBytes);
    std::vector<std::vector<std::uint8_t>> frames;
    frames.reserve(chunkCount);

    for (std::uint16_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        const auto chunkOffset = static_cast<std::size_t>(chunkIndex) * framePayloadBytes;
        const auto chunkBytes = std::min(framePayloadBytes, itemBytes.size() - chunkOffset);
        std::vector<std::uint8_t> frame(FrameHeaderBytes + chunkBytes);
        std::copy(Magic.begin(), Magic.end(), frame.begin());
        frame[4] = TransportVersion;
        frame[5] = ExpectedFlags(chunkIndex, chunkCount);
        Write16(frame, 6, static_cast<std::uint16_t>(FrameHeaderBytes));
        Write32(frame, 8, transferId);
        Write32(frame, 12, static_cast<std::uint32_t>(itemBytes.size()));
        Write32(frame, 16, checksum);
        Write32(frame, 20, static_cast<std::uint32_t>(chunkOffset));
        Write16(frame, 24, chunkIndex);
        Write16(frame, 26, chunkCount);
        Write16(frame, 28, static_cast<std::uint16_t>(chunkBytes));
        Write16(frame, 30, 0);
        std::copy_n(itemBytes.begin() + chunkOffset, chunkBytes, frame.begin() + FrameHeaderBytes);
        frames.push_back(std::move(frame));
    }
    return frames;
}

FrameInfo ParseFrame(std::span<const std::uint8_t> frame, const TransportOptions& options) {
    ValidateOptions(options);
    if (frame.size() < FrameHeaderBytes || frame.size() > options.frameBytes) {
        throw std::length_error("frame length is invalid");
    }
    if (!std::equal(Magic.begin(), Magic.end(), frame.begin())) {
        throw std::invalid_argument("frame magic is invalid");
    }
    if (frame[4] != TransportVersion) throw std::invalid_argument("unsupported version");
    if (Read16(frame, 6) != FrameHeaderBytes || Read16(frame, 30) != 0) {
        throw std::invalid_argument("frame header is invalid");
    }

    FrameInfo result{
        .transferId = Read32(frame, 8),
        .totalItemBytes = Read32(frame, 12),
        .itemChecksum = Read32(frame, 16),
        .chunkOffset = Read32(frame, 20),
        .chunkIndex = Read16(frame, 24),
        .chunkCount = Read16(frame, 26),
        .payload = frame.subspan(FrameHeaderBytes),
    };
    const auto declaredChunkBytes = Read16(frame, 28);
    if (result.totalItemBytes == 0 || result.totalItemBytes > options.maxItemBytes) {
        throw std::length_error("declared item length is invalid");
    }
    if (result.chunkCount == 0 || result.chunkCount > result.totalItemBytes
        || result.chunkIndex >= result.chunkCount) {
        throw std::invalid_argument("chunk index/count is invalid");
    }
    if (frame[5] != ExpectedFlags(result.chunkIndex, result.chunkCount)) {
        throw std::invalid_argument("chunk flags are invalid");
    }
    if (declaredChunkBytes == 0 || declaredChunkBytes != result.payload.size()) {
        throw std::invalid_argument("chunk length is invalid");
    }
    if (result.chunkOffset >= result.totalItemBytes
        || result.payload.size() > result.totalItemBytes - result.chunkOffset) {
        throw std::invalid_argument("chunk range is invalid");
    }
    return result;
}

struct Reassembler::Impl {
    struct Transfer {
        std::uint64_t createdAtMs{};
        std::vector<std::uint8_t> envelopeIdentity;
        std::uint32_t totalItemBytes{};
        std::uint32_t itemChecksum{};
        std::uint16_t chunkCount{};
        std::size_t receivedChunks{};
        std::vector<std::optional<std::vector<std::uint8_t>>> frames;
    };
    std::unordered_map<std::string, Transfer> transfers;
};

Reassembler::Reassembler(TransportOptions options)
    : options_(options), impl_(new Impl) {
    ValidateOptions(options_);
}

Reassembler::Reassembler(Reassembler&& other) noexcept
    : options_(other.options_), impl_(other.impl_) {
    other.impl_ = nullptr;
}

Reassembler& Reassembler::operator=(Reassembler&& other) noexcept {
    if (this == &other) return *this;
    delete impl_;
    options_ = other.options_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

Reassembler::~Reassembler() { delete impl_; }

AcceptResult Reassembler::Accept(
    std::span<const std::uint8_t> frame,
    std::string_view channelKey,
    std::span<const std::uint8_t> envelopeIdentity,
    std::uint64_t nowMs) {
    if (!impl_) throw std::logic_error("reassembler has been moved from");
    if (envelopeIdentity.empty()) throw std::invalid_argument("envelopeIdentity is empty");
    const auto parsed = ParseFrame(frame, options_);
    Expire(nowMs);
    const auto key = TransferKey(channelKey, parsed.transferId);
    auto iterator = impl_->transfers.find(key);
    if (iterator == impl_->transfers.end()) {
        if (impl_->transfers.size() >= options_.maxInFlightTransfers) {
            throw std::runtime_error("in-flight transfer limit reached");
        }
        Impl::Transfer transfer{
            .createdAtMs = nowMs,
            .envelopeIdentity = {envelopeIdentity.begin(), envelopeIdentity.end()},
            .totalItemBytes = parsed.totalItemBytes,
            .itemChecksum = parsed.itemChecksum,
            .chunkCount = parsed.chunkCount,
            .frames = std::vector<std::optional<std::vector<std::uint8_t>>>(parsed.chunkCount),
        };
        iterator = impl_->transfers.emplace(key, std::move(transfer)).first;
    }

    auto& transfer = iterator->second;
    const auto consistent = transfer.totalItemBytes == parsed.totalItemBytes
        && transfer.itemChecksum == parsed.itemChecksum
        && transfer.chunkCount == parsed.chunkCount
        && std::equal(transfer.envelopeIdentity.begin(), transfer.envelopeIdentity.end(),
            envelopeIdentity.begin(), envelopeIdentity.end());
    if (!consistent || transfer.frames[parsed.chunkIndex].has_value()) {
        impl_->transfers.erase(iterator);
        throw std::runtime_error(consistent ? "duplicate chunk" : "transfer metadata changed");
    }

    transfer.frames[parsed.chunkIndex] = std::vector<std::uint8_t>(frame.begin(), frame.end());
    ++transfer.receivedChunks;
    if (transfer.receivedChunks != transfer.chunkCount) {
        return {
            .status = AcceptResult::Status::Pending,
            .transferId = parsed.transferId,
            .receivedChunks = transfer.receivedChunks,
            .chunkCount = transfer.chunkCount,
        };
    }

    std::vector<std::uint8_t> item(transfer.totalItemBytes);
    std::size_t expectedOffset{};
    for (std::uint16_t index = 0; index < transfer.chunkCount; ++index) {
        if (!transfer.frames[index]) {
            impl_->transfers.erase(iterator);
            throw std::runtime_error("missing chunk");
        }
        const auto chunk = ParseFrame(*transfer.frames[index], options_);
        if (chunk.chunkOffset != expectedOffset) {
            impl_->transfers.erase(iterator);
            throw std::runtime_error("chunks are not contiguous");
        }
        std::copy(chunk.payload.begin(), chunk.payload.end(), item.begin() + expectedOffset);
        expectedOffset += chunk.payload.size();
    }
    if (expectedOffset != item.size() || Crc32(item) != transfer.itemChecksum) {
        impl_->transfers.erase(iterator);
        throw std::runtime_error("item checksum or coverage is invalid");
    }
    impl_->transfers.erase(iterator);
    return {
        .status = AcceptResult::Status::Complete,
        .transferId = parsed.transferId,
        .receivedChunks = parsed.chunkCount,
        .chunkCount = parsed.chunkCount,
        .itemBytes = std::move(item),
    };
}

std::size_t Reassembler::Expire(std::uint64_t nowMs) noexcept {
    if (!impl_) return 0;
    std::size_t expired{};
    for (auto iterator = impl_->transfers.begin(); iterator != impl_->transfers.end();) {
        const auto age = nowMs >= iterator->second.createdAtMs
            ? nowMs - iterator->second.createdAtMs
            : 0;
        if (age >= options_.reassemblyTimeoutMs) {
            iterator = impl_->transfers.erase(iterator);
            ++expired;
        } else {
            ++iterator;
        }
    }
    return expired;
}

std::size_t Reassembler::InFlightTransfers() const noexcept {
    return impl_ ? impl_->transfers.size() : 0;
}

void Reassembler::Clear() noexcept {
    if (impl_) impl_->transfers.clear();
}

} // namespace ruffneck::extended_item_stats
