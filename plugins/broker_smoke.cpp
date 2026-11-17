#include "raw_reference_broker.h"
#include <array>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr); std::abort(); } } while (0)
#include <cstdio>
extern "C" int __cdecl BridgeCopyLatestReference(std::uint32_t, float*, float*, BridgeReferenceSegment*, std::uint32_t, BridgeReferenceInfo*);
int main() {
    std::array<float, 16384> a{}, b{}, left{}, right{};
    for (int i = 0; i < 16384; ++i) { a[i] = i * 0.0001f; b[i] = -a[i]; }
    std::array<BridgeReferenceSegment, 1024> segments{};
    BridgeReferenceInfo info{};
    publishRawReference(1, 7, 48000, 0, true, 0, 2, a.data(), b.data(), 100, true);
    publishRawReference(1, 7, 48000, 100, true, 0, 2, a.data()+100, b.data()+100, 200, true);
    CHECK(BridgeCopyLatestReference(300, left.data(), right.data(), segments.data(), segments.size(), &info));
    CHECK(info.sourceId == 1 && info.epoch == 7 && info.segmentCount == 2);
    CHECK(segments[0].offset == 0 && segments[0].count == 100 && segments[1].offset == 100 && segments[1].count == 200);
    for (int i = 0; i < 300; ++i) CHECK(left[i] == a[i] && right[i] == b[i]);
    CHECK(!BridgeCopyLatestReference(300, left.data(), right.data(), segments.data(), segments.size(), &info));
    publishRawReference(2, 9, 48000, 0, true, 0, 2, a.data(), b.data(), 400, true);
    CHECK(BridgeCopyLatestReference(400, left.data(), right.data(), segments.data(), segments.size(), &info));
    CHECK(info.sourceId == 2 && info.epoch == 9);
    publishRawReference(1, 7, 48000, 300, true, 0, 2, a.data(), b.data(), 100, true);
    CHECK(!BridgeCopyLatestReference(101, left.data(), right.data(), segments.data(), segments.size(), &info));
    CHECK(BridgeCopyLatestReference(100, left.data(), right.data(), segments.data(), segments.size(), &info));
    CHECK(info.sourceId == 1 && info.epoch == 7); // worker source switch must not change render epoch
    CHECK(info.serialEnd == 400); // source-local position, not interleaved worker frame count
    publishRawReference(1, 8, 48000, 0, true, 0, 2, a.data(), b.data(), 16384, true);
    publishRawReference(1, 8, 48000, 16384, true, 0, 2, a.data(), b.data(), 16384, true);
    publishRawReference(1, 8, 48000, 32768, true, 0, 2, a.data(), b.data(), 16384, true);
    CHECK(BridgeCopyLatestReference(16384, left.data(), right.data(), segments.data(), segments.size(), &info));
    CHECK(info.epoch == 8 && info.segmentCount == 1 && segments[0].projectTimeSamples == 32768);
    for (int i = 0; i < 16384; ++i) CHECK(left[i] == a[i] && right[i] == b[i]);
    publishRawReference(1, 8, 48000, 49152, true, 0, 2, nullptr, nullptr, 4096, false);
    CHECK(!BridgeCopyLatestReference(4096, left.data(), right.data(), segments.data(), segments.size(), &info));
    // Both producer and broker use this exact rounding predicate. Sub-block
    // lengths and serial positions must remain untouched by clock tolerance.
    CHECK(bridgeProjectContinuous(100, 100));
    CHECK(bridgeProjectContinuous(99, 100));
    CHECK(bridgeProjectContinuous(101, 100));
    CHECK(!bridgeProjectContinuous(98, 100));
    CHECK(!bridgeProjectContinuous(102, 100));
    CHECK(bridgeProjectContinuous(INT64_MIN, INT64_MIN));
    CHECK(bridgeProjectContinuous(INT64_MAX, INT64_MAX));
    CHECK(!bridgeProjectContinuous(INT64_MIN, INT64_MAX));
    publishRawReference(9, 20, 48000, 0, true, 0, 2, a.data(), b.data(), 100, true, 100);
    publishRawReference(9, 20, 48000, 101, true, 0, 2, a.data()+100, b.data()+100, 100, true, 200);
    publishRawReference(9, 20, 48000, 200, true, 0, 2, a.data()+200, b.data()+200, 100, true, 300);
    CHECK(BridgeCopyLatestReference(300, left.data(), right.data(), segments.data(), segments.size(), &info));
    CHECK(info.epoch == 20 && info.serialEnd == 300 && info.segmentCount == 3);
    CHECK(segments[1].offset == 100 && segments[2].offset == 200);
    for (int i = 0; i < 300; ++i) CHECK(left[i] == a[i] && right[i] == b[i]);
    publishRawReference(9, 20, 48000, 302, true, 0, 2, a.data(), b.data(), 100, true, 400);
    CHECK(!BridgeCopyLatestReference(200, left.data(), right.data(), segments.data(), segments.size(), &info));
    CHECK(BridgeCopyLatestReference(100, left.data(), right.data(), segments.data(), segments.size(), &info));
    std::puts("broker smoke passed: +/-1 rounding preserved, 2-sample seek reset,  concatenation, segments, one-consumer, source isolation, stable epoch, ring wrap, invalid bypass");
}
