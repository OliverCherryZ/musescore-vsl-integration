#include "raw_reference_broker.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr std::size_t capacity = BRIDGE_REFERENCE_MAX_FRAMES * 2;
struct Segment {
    std::uint64_t begin = 0, end = 0;
    std::int64_t project = 0;
    std::int32_t incoming = 0, actual = 0;
};
struct Reference {
    std::array<float, capacity> left{}, right{};
    std::array<Segment, BRIDGE_REFERENCE_MAX_SEGMENTS> segments{};
    std::uint64_t serial = 0, lastConsumed = 0, source = 0, renderEpoch = 0, epoch = 0;
    std::uint64_t sourceSerialEnd = 0;
    std::size_t validFrames = 0, segmentHead = 0, segmentCount = 0;
    double sampleRate = 0;
    std::int64_t expectedProject = 0;
    bool clockWasPlaying = false;
    void clear() { validFrames = 0; segmentHead = 0; segmentCount = 0; }
};
// The source and its first track effect run synchronously in the same channel
// job on one audio worker. No locks, allocations, timestamps or cross-thread
// shared pointers are needed in that hot path.
thread_local Reference reference;
}

void publishRawReference(std::uint64_t source, std::uint64_t renderEpoch,
                         double sampleRate, std::int64_t project,
                         bool playingClock, std::int32_t incoming,
                         std::int32_t actual, const float* left,
                         const float* right, std::uint32_t frames, bool valid,
                         std::uint64_t sourceSerialEnd) {
    auto& r = reference;
    if (!valid || !left || !right || !frames || frames > capacity || !std::isfinite(sampleRate) || sampleRate <= 0) {
        r.clear(); return;
    }
    const bool discontinuity = r.source == source && playingClock && r.clockWasPlaying && !bridgeProjectContinuous(project, r.expectedProject);
    if (r.source != source || r.renderEpoch != renderEpoch || r.sampleRate != sampleRate || discontinuity || playingClock != r.clockWasPlaying) {
        r.clear();
    }
    r.source = source; r.renderEpoch = renderEpoch; r.sampleRate = sampleRate;
    r.epoch = renderEpoch;
    r.sourceSerialEnd = sourceSerialEnd ? sourceSerialEnd : static_cast<std::uint64_t>(std::max<std::int64_t>(0, project) + frames);
    r.clockWasPlaying = playingClock; r.expectedProject = project + frames;
    const std::uint64_t begin = r.serial;
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::size_t at = static_cast<std::size_t>((begin + i) % capacity);
        r.left[at] = left[i]; r.right[at] = right[i];
    }
    r.serial += frames;
    r.validFrames = std::min<std::size_t>(capacity, r.validFrames + frames);
    r.segments[r.segmentHead] = {begin, r.serial, project, incoming, actual};
    r.segmentHead = (r.segmentHead + 1) % r.segments.size();
    r.segmentCount = std::min(r.segmentCount + 1, r.segments.size());
}

extern "C" __declspec(dllexport) int __cdecl BridgeCopyLatestReference(
    std::uint32_t frames, float* left, float* right,
    BridgeReferenceSegment* segments, std::uint32_t segmentCapacity,
    BridgeReferenceInfo* info) {
    auto& r = reference;
    if (!frames || frames > BRIDGE_REFERENCE_MAX_FRAMES || !left || !right || !segments || !info ||
        r.validFrames < frames || r.lastConsumed == r.serial || !r.segmentCount) return 0;
    const std::uint64_t begin = r.serial - frames;
    std::uint64_t covered = begin;
    std::uint32_t needed = 0;
    const std::size_t oldest = (r.segmentHead + r.segments.size() - r.segmentCount) % r.segments.size();
    // Validate before touching caller buffers.
    for (std::size_t i = 0; i < r.segmentCount; ++i) {
        const auto& s = r.segments[(oldest + i) % r.segments.size()];
        if (s.end <= begin || s.begin >= r.serial) continue;
        const auto from = std::max(s.begin, begin), to = std::min(s.end, r.serial);
        if (from != covered || to <= from || ++needed > segmentCapacity) return 0;
        covered = to;
    }
    if (covered != r.serial || !needed) return 0;
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto at = static_cast<std::size_t>((begin + i) % capacity);
        left[i] = r.left[at]; right[i] = r.right[at];
    }
    std::uint32_t index = 0;
    for (std::size_t i = 0; i < r.segmentCount; ++i) {
        const auto& s = r.segments[(oldest + i) % r.segments.size()];
        if (s.end <= begin || s.begin >= r.serial) continue;
        const auto from = std::max(s.begin, begin), to = std::min(s.end, r.serial);
        segments[index++] = {static_cast<std::uint32_t>(from - begin), static_cast<std::uint32_t>(to - from),
                             s.project + static_cast<std::int64_t>(from - s.begin), s.incoming, s.actual};
    }
    *info = {BRIDGE_REFERENCE_API_VERSION, frames, index, 1, r.source, r.epoch, r.sourceSerialEnd, r.sampleRate};
    r.lastConsumed = r.serial;
    return 1;
}
