#include <cstdlib>
#include <iostream>
#include <vector>
#include "framework/audio/engine/internal/preparationrequest.h"

using muse::audio::engine::PreparationRequest;
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { std::cerr << "Failed line " << __LINE__ << ": " << #condition << '\n'; std::exit(1); } } while (false)

int main()
{
    constexpr int Cancel = -1;
    PreparationRequest<int> request;
    std::vector<int> first, second;
    const auto g1 = request.begin([&](int result) { first.push_back(result); }, Cancel);
    CHECK(g1 != 0 && request.isCurrent(g1));
    const auto g2 = request.begin([&](int result) { second.push_back(result); }, Cancel);
    CHECK(first == std::vector<int> { Cancel });
    CHECK(!request.isCurrent(g1) && request.isCurrent(g2));
    CHECK(!request.finish(g1, 1)); // late notification must not finish the new request
    CHECK(second.empty());
    CHECK(request.finish(g2, 42));
    CHECK(second == std::vector<int> { 42 });
    CHECK(!request.finish(g2, 43));
    CHECK(second.size() == 1);
    const auto g3 = request.begin([&](int result) { second.push_back(result); }, Cancel);
    request.cancel(Cancel); // same path used by stop/pause even when already stopped
    request.cancel(Cancel);
    CHECK(second == (std::vector<int> { 42, Cancel }));
    CHECK(!request.finish(g3, 1));

    // Reentrancy: cancelling the old request starts a still newer request.
    PreparationRequest<int> nested;
    std::uint64_t newest = 0;
    std::vector<int> interrupted, latest;
    nested.begin([&](int) { newest = nested.begin([&](int r) { latest.push_back(r); }, Cancel); }, Cancel);
    const auto superseded = nested.begin([&](int r) { interrupted.push_back(r); }, Cancel);
    CHECK(superseded == 0);
    CHECK(interrupted == std::vector<int> { Cancel });
    CHECK(newest != 0 && nested.isCurrent(newest));
    CHECK(nested.finish(newest, 7));
    CHECK(latest == std::vector<int> { 7 });

    // A successful completion may itself prepare another score revision.
    PreparationRequest<int> afterSuccess;
    std::uint64_t next = 0;
    int result = 0;
    auto active = afterSuccess.begin([&](int) {
        next = afterSuccess.begin([&](int r) { result = r; }, Cancel);
    }, Cancel);
    CHECK(afterSuccess.finish(active, 1));
    CHECK(afterSuccess.isCurrent(next));
    CHECK(!afterSuccess.finish(active, 2));
    CHECK(afterSuccess.finish(next, 99));
    CHECK(result == 99);
    std::cout << checks << " actual PreparationRequest assertions passed\n";
}
