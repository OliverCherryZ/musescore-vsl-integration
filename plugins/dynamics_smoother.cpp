// Local, host-specific first-insert FX. It smooths measured host gain only.
// Raw reference is supplied by Synchron Smooth Bridge on the same audio thread.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "raw_reference_api.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
HMODULE selfModule = nullptr;
std::mutex logMutex;
std::atomic<std::uint64_t> nextInstance{1};
const FUID classID(0x1BB4D46D, 0x04F14C79, 0xB9B984A2, 0x96631A37);
constexpr char pluginName[] = "VSL Host Dynamics Smoother";
constexpr char pluginVendor[] = "Local Diagnostic Bridge";
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double minimumTransitionMs = 0.5;
constexpr double maximumTransitionMs = 100.0;
constexpr double defaultTransitionMs = 20.0;
constexpr ParamID transitionParameter = 0;

std::wstring moduleDirectory() {
    wchar_t path[32768]{};
    const DWORD size = GetModuleFileNameW(selfModule, path, 32768);
    if (!size || size >= 32768) return {};
    const std::wstring full(path, size);
    const auto index = full.find_last_of(L"\\/");
    return index == std::wstring::npos ? std::wstring{} : full.substr(0, index);
}

// Only called during initialization, setup, stopping, or destruction.
void logLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex);
    const std::wstring directory = moduleDirectory();
    if (directory.empty()) return;
    const std::wstring path = directory + L"\\smoother_diagnostics.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME time{}; GetLocalTime(&time);
    char prefix[128]{};
    std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu ",
                  time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                  time.wMilliseconds, static_cast<unsigned long>(GetCurrentProcessId()));
    const std::string text = std::string(prefix) + line + "\r\n";
    DWORD written = 0; WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

bool sameID(const char* left, const char* right) { return left && right && std::memcmp(left, right, 16) == 0; }
template<class T> bool isID(const char* id) { return sameID(id, T::iid); }
template<std::size_t N> void writeWide(char16 (&destination)[N], const char* source) {
    std::fill(destination, destination + N, 0);
    for (std::size_t index = 0; index + 1 < N && source[index]; ++index)
        destination[index] = static_cast<unsigned char>(source[index]);
}

struct Statistics {
    std::uint64_t calls = 0, frames = 0, matchedBlocks = 0, matchedFrames = 0;
    std::uint64_t smoothedFrames = 0, untouchedFrames = 0, lowEnergyFrames = 0;
    std::uint64_t missingReferenceBlocks = 0, invalidReferenceBlocks = 0, layoutBypassBlocks = 0;
    std::uint64_t transitions = 0, identityResets = 0, gainLogOverflow = 0;
    double maximumRelativeError = 0;
};
struct GainRecord {
    std::uint64_t source = 0, epoch = 0, serial = 0;
    std::int64_t project = 0;
    double before = 0, after = 0;
};

class GainSmootherCore {
public:
    Statistics stats;
    std::array<GainRecord, 64> gainRecords{};
    std::size_t gainRecordCount = 0;

    void reset() {
        initialized_ = false; identityKnown_ = false;
        rampIndex_ = rampLength_ = 0; current_ = target_ = rampStart_ = 0;
    }
    void noteCall(std::uint32_t frames) { ++stats.calls; stats.frames += frames; }
    void missingReference() { ++stats.missingReferenceBlocks; reset(); }
    void unsupportedLayout() { ++stats.layoutBypassBlocks; reset(); }

    // Validate the complete block before changing any output sample or DSP state.
    // All scratch storage is fixed; this function does no allocation or I/O.
    bool apply(const float* inputL, const float* inputR, float* outputL, float* outputR,
               const float* rawL, const float* rawR, const BridgeReferenceSegment* segments,
               const BridgeReferenceInfo& info, std::uint32_t frames,
               double expectedSampleRate, double transitionMs) {
        if (!inputL || !inputR || !outputL || !outputR || !rawL || !rawR || !segments ||
            info.apiVersion != BRIDGE_REFERENCE_API_VERSION || info.frames != frames ||
            !frames || frames > BRIDGE_REFERENCE_MAX_FRAMES ||
            !info.segmentCount || info.segmentCount > BRIDGE_REFERENCE_MAX_SEGMENTS ||
            !(info.flags & 1u) || !info.sourceId ||
            !std::isfinite(info.sampleRate) || info.sampleRate <= 0 ||
            std::fabs(info.sampleRate - expectedSampleRate) > 0.01) return reject();

        std::uint32_t covered = 0;
        double blockError = 0, blockInputEnergy = 0;
        for (std::uint32_t index = 0; index < info.segmentCount; ++index) {
            const auto& segment = segments[index];
            if (!segment.count || segment.offset != covered || segment.count > frames - covered) return reject();
            double rawEnergy = 0, inputEnergy = 0, dot = 0;
            for (std::uint32_t frame = segment.offset; frame < segment.offset + segment.count; ++frame) {
                const double xl = rawL[frame], xr = rawR[frame], yl = inputL[frame], yr = inputR[frame];
                if (!std::isfinite(xl) || !std::isfinite(xr) || !std::isfinite(yl) || !std::isfinite(yr)) return reject();
                rawEnergy += xl * xl + xr * xr;
                inputEnergy += yl * yl + yr * yr;
                dot += xl * yl + xr * yr;
            }
            const double floor = 2.0 * segment.count * 1e-24;
            if (rawEnergy <= 2.0 * segment.count * 1e-18) {
                // Below -180 dBFS RMS the ratio is uninformative. Never amplify it.
                if (inputEnergy > 4.0 * rawEnergy + floor) return reject();
                estimates_[index] = -1;
                covered += segment.count;
                continue;
            }
            const double gain = dot / rawEnergy;
            if (!std::isfinite(gain) || gain < 0 || gain > 2) return reject();
            double error = 0;
            for (std::uint32_t frame = segment.offset; frame < segment.offset + segment.count; ++frame) {
                const double dl = inputL[frame] - rawL[frame] * gain;
                const double dr = inputR[frame] - rawR[frame] * gain;
                error += dl * dl + dr * dr;
            }
            // The reference must explain the input by a scalar, not merely correlate.
            if (error > 1e-10 * inputEnergy + floor) return reject();
            blockError += error; blockInputEnergy += inputEnergy;
            estimates_[index] = gain;
            covered += segment.count;
        }
        if (covered != frames || blockError > 1e-10 * blockInputEnergy + 2.0 * frames * 1e-24) return reject();
        const double relativeError = blockError / std::max(blockInputEnergy, 1e-30);
        stats.maximumRelativeError = std::max(stats.maximumRelativeError, relativeError);

        if (!identityKnown_ || source_ != info.sourceId || epoch_ != info.epoch ||
            sampleRate_ != info.sampleRate || serialEnd_ + frames != info.serialEnd) {
            initialized_ = false; rampIndex_ = rampLength_ = 0;
            if (identityKnown_) ++stats.identityResets;
        }
        identityKnown_ = true; source_ = info.sourceId; epoch_ = info.epoch;
        sampleRate_ = info.sampleRate; serialEnd_ = info.serialEnd;
        ++stats.matchedBlocks; stats.matchedFrames += frames;
        const std::uint32_t duration = static_cast<std::uint32_t>(std::max(2.0,
            std::round(sampleRate_ * std::clamp(transitionMs, minimumTransitionMs, maximumTransitionMs) / 1000.0)));

        for (std::uint32_t index = 0; index < info.segmentCount; ++index) {
            const auto& segment = segments[index];
            const double gain = estimates_[index];
            if (gain >= 0) {
                if (!initialized_) {
                    current_ = target_ = gain; initialized_ = true; rampIndex_ = rampLength_ = 0;
                } else if (std::fabs(gain - target_) > 1e-6) {
                    const double old = current_;
                    rampStart_ = current_; target_ = gain; rampIndex_ = 0; rampLength_ = duration;
                    ++stats.transitions;
                    if (gainRecordCount < gainRecords.size())
                        gainRecords[gainRecordCount++] = {source_, epoch_, info.serialEnd - frames + segment.offset,
                                                         segment.projectTimeSamples, old, gain};
                    else ++stats.gainLogOverflow;
                }
            }
            for (std::uint32_t frame = segment.offset; frame < segment.offset + segment.count; ++frame) {
                const bool smoothing = rampIndex_ < rampLength_;
                if (smoothing) advanceRamp();
                if (gain < 0) {
                    outputL[frame] = inputL[frame]; outputR[frame] = inputR[frame];
                    ++stats.lowEnergyFrames;
                } else if (smoothing) {
                    outputL[frame] = static_cast<float>(rawL[frame] * current_);
                    outputR[frame] = static_cast<float>(rawR[frame] * current_);
                    ++stats.smoothedFrames;
                } else {
                    // Steady-state samples are bit-identical to host input.
                    outputL[frame] = inputL[frame]; outputR[frame] = inputR[frame];
                    ++stats.untouchedFrames;
                }
            }
        }
        return true;
    }
private:
    bool reject() { ++stats.invalidReferenceBlocks; reset(); return false; }
    void advanceRamp() {
        const double phase = static_cast<double>(rampIndex_) / static_cast<double>(rampLength_ - 1);
        const double weight = 0.5 - 0.5 * std::cos(pi * phase);
        current_ = rampStart_ + (target_ - rampStart_) * weight;
        ++rampIndex_;
        if (rampIndex_ == rampLength_) current_ = target_;
    }
    std::array<double, BRIDGE_REFERENCE_MAX_SEGMENTS> estimates_{};
    bool initialized_ = false, identityKnown_ = false;
    std::uint64_t source_ = 0, epoch_ = 0, serialEnd_ = 0;
    double sampleRate_ = 0, current_ = 0, target_ = 0, rampStart_ = 0;
    std::uint32_t rampIndex_ = 0, rampLength_ = 0;
};

class DynamicsSmoother final : public IComponent, public IAudioProcessor, public IEditController {
public:
    DynamicsSmoother() : instance_(nextInstance.fetch_add(1)) {}
    ~DynamicsSmoother() { flush("destroy"); if (handler_) handler_->release(); if (bridgeModule_) FreeLibrary(bridgeModule_); }
    tresult PLUGIN_API queryInterface(const TUID id, void** object) override {
        if (!object) return kInvalidArgument; *object = nullptr;
        if (isID<FUnknown>(id) || isID<IPluginBase>(id) || isID<IComponent>(id)) *object = static_cast<IComponent*>(this);
        else if (isID<IAudioProcessor>(id)) *object = static_cast<IAudioProcessor*>(this);
        else if (isID<IEditController>(id)) *object = static_cast<IEditController*>(this);
        else return kNoInterface;
        addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++references_; }
    uint32 PLUGIN_API release() override { auto count = --references_; if (!count) delete this; return count; }
    tresult PLUGIN_API initialize(FUnknown*) override {
        if (initialized_) return kResultFalse;
        initialized_ = true;
        const auto directory = moduleDirectory();
        if (!directory.empty()) {
            const auto path = directory + L"\\Synchron Smooth Bridge.vst3";
            bridgeModule_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (bridgeModule_) copyReference_ = reinterpret_cast<BridgeCopyLatestReferenceFn>(
                GetProcAddress(bridgeModule_, "BridgeCopyLatestReference"));
        }
        logLine(label() + "initialize referenceAPI=" + std::to_string(copyReference_ != nullptr) + " defaultTransitionMs=" + std::to_string(defaultTransitionMs));
        return kResultOk; // Missing companion means transparent bypass, never silence.
    }
    tresult PLUGIN_API terminate() override { flush("terminate"); initialized_ = false; return kResultOk; }
    tresult PLUGIN_API getControllerClassId(TUID id) override { if (id) std::memset(id, 0, 16); return kResultFalse; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override { return type == kAudio ? 1 : 0; }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo& info) override {
        if (type != kAudio || index != 0 || (direction != kInput && direction != kOutput)) return kInvalidArgument;
        info = {}; info.mediaType = kAudio; info.direction = direction; info.channelCount = 2;
        info.busType = kMain; info.flags = BusInfo::kDefaultActive;
        writeWide(info.name, direction == kInput ? "Stereo Input" : "Stereo Output"); return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection, int32 index, TBool) override {
        return type == kAudio && index == 0 ? kResultOk : kInvalidArgument;
    }
    tresult PLUGIN_API setActive(TBool active) override { if (!active) resetRequested_.store(true); return kResultOk; }
    tresult PLUGIN_API setState(IBStream* stream) override {
        if (!stream) return kInvalidArgument;
        std::array<unsigned char, 20> bytes{}; int32 read = 0;
        if (stream->read(bytes.data(), static_cast<int32>(bytes.size()), &read) != kResultOk || read != bytes.size()) return kResultFalse;
        if (std::memcmp(bytes.data(), "VSLDYN01", 8) != 0) return kResultFalse;
        std::uint32_t version = 0; double value = 0;
        std::memcpy(&version, bytes.data() + 8, 4); std::memcpy(&value, bytes.data() + 12, 8);
        if (version != 1 || !std::isfinite(value) || value < minimumTransitionMs || value > maximumTransitionMs) return kResultFalse;
        transitionMs_.store(value); resetRequested_.store(true); return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream) override {
        if (!stream) return kInvalidArgument;
        std::array<unsigned char, 20> bytes{}; const std::uint32_t version = 1; const double value = transitionMs_.load();
        std::memcpy(bytes.data(), "VSLDYN01", 8); std::memcpy(bytes.data() + 8, &version, 4); std::memcpy(bytes.data() + 12, &value, 8);
        int32 written = 0; const auto result = stream->write(bytes.data(), static_cast<int32>(bytes.size()), &written);
        return result == kResultOk && written == bytes.size() ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API setComponentState(IBStream* stream) override { return setState(stream); }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numInputs, SpeakerArrangement* outputs, int32 numOutputs) override {
        return inputs && outputs && numInputs == 1 && numOutputs == 1 && inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo
            ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32 index, SpeakerArrangement& arrangement) override {
        if (index != 0) return kInvalidArgument; arrangement = SpeakerArr::kStereo; return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultOk : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if (setup.symbolicSampleSize != kSample32 || !std::isfinite(setup.sampleRate) || setup.sampleRate <= 0 || setup.maxSamplesPerBlock <= 0)
            return kResultFalse;
        sampleRate_ = setup.sampleRate; resetRequested_.store(true);
        logLine(label() + "setup sampleRate=" + std::to_string(sampleRate_) + " maxSamples=" + std::to_string(setup.maxSamplesPerBlock) +
                " transitionMs=" + std::to_string(transitionMs_.load())); return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool active) override {
        if (!active) { flush("setProcessing(false)"); resetRequested_.store(true); }
        return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData& data) override {
        if (data.numSamples <= 0) return kResultOk;
        const auto frames = static_cast<std::uint32_t>(data.numSamples);
        core_.noteCall(frames);
        if (resetRequested_.exchange(false)) core_.reset();
        if (data.symbolicSampleSize != kSample32 || data.numInputs != 1 || data.numOutputs != 1 ||
            !data.inputs || !data.outputs || data.inputs[0].numChannels != 2 || data.outputs[0].numChannels != 2 ||
            !data.inputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32 ||
            !data.inputs[0].channelBuffers32[0] || !data.inputs[0].channelBuffers32[1] ||
            !data.outputs[0].channelBuffers32[0] || !data.outputs[0].channelBuffers32[1] || frames > BRIDGE_REFERENCE_MAX_FRAMES) {
            core_.unsupportedLayout(); passthrough(data); return kResultOk;
        }
        BridgeReferenceInfo info{};
        if (!copyReference_ || copyReference_(frames, rawLeft_.data(), rawRight_.data(), segments_.data(),
                                              static_cast<std::uint32_t>(segments_.size()), &info) != 1) {
            core_.missingReference(); passthrough(data); return kResultOk;
        }
        if (!core_.apply(data.inputs[0].channelBuffers32[0], data.inputs[0].channelBuffers32[1],
                         data.outputs[0].channelBuffers32[0], data.outputs[0].channelBuffers32[1],
                         rawLeft_.data(), rawRight_.data(), segments_.data(), info, frames, sampleRate_, transitionMs_.load())) {
            passthrough(data); return kResultOk;
        }
        // Gain smoothing can briefly produce nonzero output when the input gain is zero.
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override { return kNoTail; }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override {
        if (index != 0) return kInvalidArgument; info = {}; info.id = transitionParameter;
        writeWide(info.title, "Gain transition"); writeWide(info.shortTitle, "Transition"); writeWide(info.units, "ms");
        info.defaultNormalizedValue = plainParamToNormalized(transitionParameter, defaultTransitionMs);
        info.stepCount = 0; info.unitId = 0; info.flags = 0; return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 text) override {
        if (id != transitionParameter || !text) return kInvalidArgument;
        char ascii[64]{}; std::snprintf(ascii, sizeof(ascii), "%.3f", normalizedParamToPlain(id, value));
        for (int index = 0; index < 128; ++index) text[index] = 0;
        for (int index = 0; ascii[index] && index < 127; ++index) text[index] = ascii[index]; return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID id, TChar* text, ParamValue& value) override {
        if (id != transitionParameter || !text) return kInvalidArgument; char ascii[128]{};
        for (int index = 0; text[index] && index < 127; ++index) ascii[index] = text[index] <= 127 ? static_cast<char>(text[index]) : ' ';
        char* end = nullptr; const double plain = std::strtod(ascii, &end);
        if (end == ascii || !std::isfinite(plain) || plain < minimumTransitionMs || plain > maximumTransitionMs) return kResultFalse;
        value = plainParamToNormalized(id, plain); return kResultOk;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue value) override {
        return id == transitionParameter ? minimumTransitionMs + (maximumTransitionMs - minimumTransitionMs) * std::clamp(value, 0.0, 1.0) : 0;
    }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue value) override {
        return id == transitionParameter ? std::clamp((value - minimumTransitionMs) / (maximumTransitionMs - minimumTransitionMs), 0.0, 1.0) : 0;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return plainParamToNormalized(id, transitionMs_.load()); }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override {
        if (id != transitionParameter || !std::isfinite(value)) return kInvalidArgument;
        transitionMs_.store(normalizedParamToPlain(id, value)); return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler* handler) override {
        if (handler) handler->addRef(); if (handler_) handler_->release(); handler_ = handler; return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString) override { return nullptr; }
private:
    static void passthrough(ProcessData& data) {
        if (!data.outputs || data.numSamples <= 0) return;
        for (int32 bus = 0; bus < data.numOutputs; ++bus) {
            auto& output = data.outputs[bus];
            const AudioBusBuffers* input = data.inputs && bus < data.numInputs ? &data.inputs[bus] : nullptr;
            output.silenceFlags = input ? input->silenceFlags : ~uint64(0);
            for (int32 channel = 0; channel < output.numChannels; ++channel) {
                if (data.symbolicSampleSize == kSample32 && output.channelBuffers32 && output.channelBuffers32[channel]) {
                    auto dest = output.channelBuffers32[channel];
                    auto src = input && channel < input->numChannels && input->channelBuffers32 ? input->channelBuffers32[channel] : nullptr;
                    if (src) std::memmove(dest, src, sizeof(float) * data.numSamples); else std::fill(dest, dest + data.numSamples, 0.f);
                } else if (data.symbolicSampleSize == kSample64 && output.channelBuffers64 && output.channelBuffers64[channel]) {
                    auto dest = output.channelBuffers64[channel];
                    auto src = input && channel < input->numChannels && input->channelBuffers64 ? input->channelBuffers64[channel] : nullptr;
                    if (src) std::memmove(dest, src, sizeof(double) * data.numSamples); else std::fill(dest, dest + data.numSamples, 0.0);
                }
            }
        }
    }
    std::string label() const { return "instance=" + std::to_string(instance_) + " "; }
    void flush(const char* reason) {
        const auto& s = core_.stats;
        if (!s.calls || lastLoggedCalls_ == s.calls) return;
        char summary[1600]{};
        std::snprintf(summary, sizeof(summary),
            "reason=%s calls=%llu frames=%llu matchedBlocks=%llu matchedFrames=%llu smoothedFrames=%llu untouchedFrames=%llu lowEnergyFrames=%llu missingReferenceBlocks=%llu invalidReferenceBlocks=%llu layoutBypassBlocks=%llu transitions=%llu identityResets=%llu maxRelativeSSE=%.12g gainLogOverflow=%llu transitionMs=%.6g",
            reason, s.calls, s.frames, s.matchedBlocks, s.matchedFrames, s.smoothedFrames, s.untouchedFrames, s.lowEnergyFrames,
            s.missingReferenceBlocks, s.invalidReferenceBlocks, s.layoutBypassBlocks, s.transitions, s.identityResets,
            s.maximumRelativeError, s.gainLogOverflow, transitionMs_.load());
        std::string lines = label() + summary;
        for (std::size_t index = lastLoggedGains_; index < core_.gainRecordCount; ++index) {
            const auto& gain = core_.gainRecords[index]; char text[320]{};
            std::snprintf(text, sizeof(text), "\r\ninstance=%llu gain source=%llu epoch=%llu serial=%llu project=%lld from=%.12g target=%.12g",
                          instance_, gain.source, gain.epoch, gain.serial, gain.project, gain.before, gain.after);
            lines += text;
        }
        logLine(lines); lastLoggedCalls_ = s.calls; lastLoggedGains_ = core_.gainRecordCount;
    }
    std::atomic<uint32> references_{1};
    std::uint64_t instance_ = 0, lastLoggedCalls_ = 0;
    std::size_t lastLoggedGains_ = 0;
    bool initialized_ = false;
    std::atomic<bool> resetRequested_{true};
    std::atomic<double> transitionMs_{defaultTransitionMs};
    double sampleRate_ = 48000;
    IComponentHandler* handler_ = nullptr;
    HMODULE bridgeModule_ = nullptr;
    BridgeCopyLatestReferenceFn copyReference_ = nullptr;
    std::array<float, BRIDGE_REFERENCE_MAX_FRAMES> rawLeft_{}, rawRight_{};
    std::array<BridgeReferenceSegment, BRIDGE_REFERENCE_MAX_SEGMENTS> segments_{};
    GainSmootherCore core_;
};

class Factory final : public IPluginFactory2 {
public:
    tresult PLUGIN_API queryInterface(const TUID id, void** object) override {
        if (!object) return kInvalidArgument; *object = nullptr;
        if (!isID<FUnknown>(id) && !isID<IPluginFactory>(id) && !isID<IPluginFactory2>(id)) return kNoInterface;
        *object = static_cast<IPluginFactory2*>(this); addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++references_; }
    uint32 PLUGIN_API release() override { auto count = --references_; if (!count) delete this; return count; }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override {
        if (!info) return kInvalidArgument; *info = {};
        std::strncpy(info->vendor, pluginVendor, sizeof(info->vendor) - 1); return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override {
        if (index != 0 || !info) return kInvalidArgument; *info = {};
        std::memcpy(info->cid, classID, 16); info->cardinality = PClassInfo::kManyInstances;
        std::strncpy(info->category, kVstAudioEffectClass, sizeof(info->category) - 1);
        std::strncpy(info->name, pluginName, sizeof(info->name) - 1); return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override {
        if (index != 0 || !info) return kInvalidArgument; *info = {};
        std::memcpy(info->cid, classID, 16); info->cardinality = PClassInfo::kManyInstances;
        std::strncpy(info->category, kVstAudioEffectClass, sizeof(info->category) - 1);
        std::strncpy(info->name, pluginName, sizeof(info->name) - 1);
        std::strncpy(info->vendor, pluginVendor, sizeof(info->vendor) - 1);
        std::strncpy(info->subCategories, "Fx|Dynamics", sizeof(info->subCategories) - 1);
        std::strncpy(info->version, "0.1.0", sizeof(info->version) - 1);
        std::strncpy(info->sdkVersion, "VST 3.7.12", sizeof(info->sdkVersion) - 1); return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** object) override {
        if (!object) return kInvalidArgument; *object = nullptr;
        if (!sameID(cid, classID)) return kInvalidArgument;
        auto* effect = new (std::nothrow) DynamicsSmoother(); if (!effect) return kOutOfMemory;
        const auto result = effect->queryInterface(iid, object); effect->release(); return result;
    }
private:
    std::atomic<uint32> references_{1};
};
} // namespace

extern "C" __declspec(dllexport) IPluginFactory* PLUGIN_API GetPluginFactory() { return new Factory(); }
extern "C" __declspec(dllexport) bool PLUGIN_API InitDll() { return true; }
extern "C" __declspec(dllexport) bool PLUGIN_API ExitDll() { return true; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { selfModule = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}

#ifdef SMOOTHER_SELF_TEST
#include <vector>
#include <stdexcept>

int main() {
    auto require = [](bool ok, const char* text) { if (!ok) throw std::runtime_error(text); };
    constexpr std::uint32_t count = 2048;
    std::array<float, count> rawL{}, rawR{}, inputL{}, inputR{}, outputL{}, outputR{};
    for (std::uint32_t i = 0; i < count; ++i) {
        rawL[i] = static_cast<float>(0.17 * std::sin((i + 3) * 0.037) + 0.03);
        rawR[i] = static_cast<float>(0.13 * std::cos((i + 5) * 0.041) - 0.01);
        const float gain = i < 173 ? 0.25f : (i < 900 ? 0.85f : 0.f);
        inputL[i] = rawL[i] * gain; inputR[i] = rawR[i] * gain;
    }
    auto render = [&](const std::vector<std::uint32_t>& partitions, float* left, float* right) {
        GainSmootherCore core; std::uint32_t position = 0;
        for (auto size : partitions) {
            std::array<BridgeReferenceSegment, 4> segments{}; std::uint32_t offset = 0, number = 0;
            while (offset < size) {
                const auto absolute = position + offset;
                const auto next = absolute < 173 ? 173u : (absolute < 900 ? 900u : count);
                const auto duration = std::min(size - offset, next - absolute);
                segments[number++] = {offset, duration, absolute, 0, 2}; offset += duration;
            }
            BridgeReferenceInfo info{1, size, number, 1, 1, 1, position + size, 48000};
            core.noteCall(size);
            require(core.apply(inputL.data() + position, inputR.data() + position, left + position, right + position,
                               rawL.data() + position, rawR.data() + position, segments.data(), info, size, 48000, 3), "matched render failed");
            position += size;
        }
        require(position == count, "partition length"); require(core.stats.transitions == 2, "unexpected transition count");
        require(core.stats.smoothedFrames == 288, "3ms duration must be 144 frames per transition");
        return core.stats;
    };
    const auto stats = render({count}, outputL.data(), outputR.data());
    require(std::memcmp(inputL.data(), outputL.data(), 173 * sizeof(float)) == 0, "constant gain must be bit exact");
    require(std::fabs(outputL[173] / rawL[173] - 0.25) < 1e-6, "first transition sample not continuous");
    require(std::fabs(outputL[316] / rawL[316] - 0.85) < 1e-6, "transition does not reach target at 3ms");
    require(outputL[1043] == 0 && outputR[1043] == 0, "zero gain transition failed");
    std::array<float, count> splitL{}, splitR{};
    render({31, 127, 128, 256, 512, 994}, splitL.data(), splitR.data());
    double partitionDifference = 0;
    for (std::uint32_t i = 0; i < count; ++i) partitionDifference = std::max(partitionDifference,
        std::max(std::fabs(static_cast<double>(splitL[i] - outputL[i])), std::fabs(static_cast<double>(splitR[i] - outputR[i]))));
    require(partitionDifference < 2e-7, "variable block sizes changed DSP result");
    GainSmootherCore badCore; BridgeReferenceSegment segment{0, 128, 0, 0, 2}; BridgeReferenceInfo info{1,128,1,1,2,1,128,48000};
    inputL[70] += 0.0001f; outputL.fill(123); outputR.fill(456);
    require(!badCore.apply(inputL.data(), inputR.data(), outputL.data(), outputR.data(), rawL.data(), rawR.data(), &segment, info, 128,48000,3), "corrupt reference accepted");
    require(outputL[0] == 123 && outputL[127] == 123 && outputR[127] == 456, "validation changed output before bypass");
    inputL.fill(0); inputR.fill(0); rawL.fill(0); rawR.fill(0);
    require(badCore.apply(inputL.data(), inputR.data(), outputL.data(), outputR.data(), rawL.data(), rawR.data(), &segment, info,128,48000,3), "silence rejected");
    require(outputL[127] == 0 && outputR[127] == 0, "silence changed");
    // A 40 ms transition must take exactly 1920 frames at 48 kHz.
    // Explicit 3 ms tests above stay unchanged to cover legacy state behavior.
    constexpr std::uint32_t longCount = 4096;
    std::array<float, longCount> longRawL{}, longRawR{}, longInputL{}, longInputR{}, longOutputL{}, longOutputR{};
    longRawL.fill(0.25f); longRawR.fill(0.125f);
    for (std::uint32_t i = 0; i < longCount; ++i) {
        const float gain = i == 0 ? 0.25f : 0.75f;
        longInputL[i] = longRawL[i] * gain; longInputR[i] = longRawR[i] * gain;
    }
    const std::array<BridgeReferenceSegment, 2> longSegments{{{0, 1, 0, 0, 2}, {1, longCount - 1, 1, 0, 2}}};
    const BridgeReferenceInfo longInfo{1, longCount, 2, 1, 3, 1, longCount, 48000};
    GainSmootherCore longCore;
    require(longCore.apply(longInputL.data(), longInputR.data(), longOutputL.data(), longOutputR.data(),
                           longRawL.data(), longRawR.data(), longSegments.data(), longInfo, longCount, 48000, 40),
            "40ms render failed");
    require(longCore.stats.transitions == 1 && longCore.stats.smoothedFrames == 1920, "40ms duration must be 1920 frames");
    for (std::uint32_t i = 0; i < 1920; ++i) {
        const double gain = 0.25 + 0.5 * (0.5 - 0.5 * std::cos(pi * double(i) / 1919.0));
        require(std::fabs(longOutputL[i + 1] - 0.25 * gain) < 1e-8, "40ms raised cosine shape differs");
    }
    require(longOutputL[1] == longInputL[0] && longOutputL[1920] == longInputL[1920], "40ms endpoints differ");
    require(std::memcmp(longInputL.data() + 1921, longOutputL.data() + 1921, (longCount - 1921) * sizeof(float)) == 0,
            "steady samples after 40ms differ");
    GainSmootherCore fiftyFiveCore;
    require(fiftyFiveCore.apply(longInputL.data(), longInputR.data(), longOutputL.data(), longOutputR.data(),
                               longRawL.data(), longRawR.data(), longSegments.data(), longInfo, longCount, 48000, 55),
            "55ms render failed");
    require(fiftyFiveCore.stats.transitions == 1 && fiftyFiveCore.stats.smoothedFrames == 2640,
            "55ms duration must be 2640 frames");
    for (std::uint32_t i = 0; i < 2640; ++i) {
        const double gain = 0.25 + 0.5 * (0.5 - 0.5 * std::cos(pi * double(i) / 2639.0));
        require(std::fabs(longOutputL[i + 1] - 0.25 * gain) < 1e-8, "55ms raised cosine shape differs");
    }
    require(longOutputL[1] == longInputL[0] && longOutputL[2640] == longInputL[2640], "55ms endpoints differ");
    require(std::memcmp(longInputL.data() + 2641, longOutputL.data() + 2641, (longCount - 2641) * sizeof(float)) == 0,
            "steady samples after 55ms differ");
    std::printf("{\"passed\":true,\"tests\":[\"steady_bit_exact\",\"raised_cosine_144_frames\",\"zero_gain\",\"variable_blocks\",\"reject_mismatch_before_write\",\"silence\",\"raised_cosine_40ms_1920_frames\",\"raised_cosine_55ms_2640_frames\"],\"partition_max_difference\":%.12g,\"maximum_relative_sse\":%.12g}\n",
                partitionDifference, stats.maximumRelativeError);
    return 0;
}
#endif
