#pragma once
#include "raw_reference_api.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include <algorithm>
#include <cstdint>

// Mirrors MuseScore 4.7.7 fillOutputBufferInstrument bus/channel order before
// its host volume multiplier. Never modifies the plugin output buffers.
// The host multiplies each bus before adding; our raw sum differs by float
// roundoff only, which the downstream scalar-residual validation checks.
inline bool sumHostOutputReference(const Steinberg::Vst::ProcessData& data,
                                   std::uint64_t activeOutputs,
                                   float* left, float* right) {
    using namespace Steinberg::Vst;
    if (!activeOutputs || !left || !right || data.symbolicSampleSize != kSample32 ||
        !data.outputs || data.numOutputs <= 0 || data.numSamples <= 0 ||
        data.numSamples > static_cast<Steinberg::int32>(BRIDGE_REFERENCE_MAX_FRAMES)) return false;
    // Validate every selected bus before writing the reference.
    for (int bus = 0; bus < 64; ++bus) {
        if (!(activeOutputs & (std::uint64_t(1) << bus))) continue;
        if (bus >= data.numOutputs) return false;
        const auto& out = data.outputs[bus];
        if (out.numChannels < 1 || out.numChannels > 2 || !out.channelBuffers32) return false;
        for (int channel = 0; channel < out.numChannels; ++channel)
            if (!out.channelBuffers32[channel]) return false;
    }
    std::fill_n(left, data.numSamples, 0.0f);
    std::fill_n(right, data.numSamples, 0.0f);
    for (int bus = 0; bus < 64; ++bus) {
        if (!(activeOutputs & (std::uint64_t(1) << bus))) continue;
        const auto& out = data.outputs[bus];
        for (Steinberg::int32 frame = 0; frame < data.numSamples; ++frame) {
            left[frame] += out.channelBuffers32[0][frame];
            if (out.numChannels > 1) right[frame] += out.channelBuffers32[1][frame];
        }
    }
    return true;
}
