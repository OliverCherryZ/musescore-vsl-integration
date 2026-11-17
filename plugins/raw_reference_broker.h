#pragma once
#include "raw_reference_api.h"

void publishRawReference(std::uint64_t source, std::uint64_t renderEpoch,
                         double sampleRate, std::int64_t projectTimeSamples,
                         bool hasPlayingClock, std::int32_t incomingMode,
                         std::int32_t actualMode, const float* left,
                         const float* right, std::uint32_t frames, bool valid,
                         std::uint64_t sourceSerialEnd = 0);
