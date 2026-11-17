#pragma once
#include <cstdint>
#include <limits>

// C ABI shared by the two local modules. Fetch runs on the same audio worker
// thread immediately after that track's synthesizer. No cross-thread pointers.
constexpr std::uint32_t BRIDGE_REFERENCE_API_VERSION = 1;
constexpr std::uint32_t BRIDGE_REFERENCE_MAX_FRAMES = 16384;
constexpr std::uint32_t BRIDGE_REFERENCE_MAX_SEGMENTS = 1024;

struct BridgeReferenceSegment {
    std::uint32_t offset;
    std::uint32_t count;
    std::int64_t projectTimeSamples;
    std::int32_t incomingMode;
    std::int32_t actualMode;
};

struct BridgeReferenceInfo {
    std::uint32_t apiVersion;
    std::uint32_t frames;
    std::uint32_t segmentCount;
    std::uint32_t flags; // bit 0: verified stereo sum of all host-selected output buses
    std::uint64_t sourceId;
    std::uint64_t epoch;
    std::uint64_t serialEnd; // source-local processed frame count, not worker/ring count
    double sampleRate;
};

using BridgeCopyLatestReferenceFn = int (__cdecl*)(
    std::uint32_t frames, float* left, float* right,
    BridgeReferenceSegment* segments, std::uint32_t segmentCapacity,
    BridgeReferenceInfo* info);

// Export name: BridgeCopyLatestReference. Returns 1 only for a new, complete
// reference; returns 0 without writing output on failure. Safe to bypass then.

// MuseScore converts between integer microseconds and samples. Accept only a
// one-sample rounding difference; true seeks/discontinuities still reset.
inline bool bridgeProjectContinuous(std::int64_t actual, std::int64_t expected) {
    return actual == expected ||
        (actual > std::numeric_limits<std::int64_t>::min() && actual - 1 == expected) ||
        (actual < std::numeric_limits<std::int64_t>::max() && actual + 1 == expected);
}
