// Diagnostic VST3 bridge. No sample, licence or installed binary modifications.
// Two component classes differ only in whether ProcessData.processMode follows
// the most recent successful setupProcessing call. All VSL state is forwarded.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#ifndef BRIDGE_VARIANT_FIX
#define BRIDGE_VARIANT_FIX 0
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
HMODULE selfModule = nullptr;
HMODULE originalModule = nullptr;
IPluginFactory* originalFactory = nullptr;
std::once_flag loadOnce;
std::mutex logMutex;
std::atomic<unsigned long> nextInstance{1};
constexpr wchar_t originalPath[] = L"C:\\Program Files\\Common Files\\VST3\\Vienna Synchron Player.vst3";

void logLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex);
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(selfModule, path, 32768);
    if (!length || length >= 32768) return;
    std::wstring dest(path, length);
    dest = dest.substr(0, dest.find_last_of(L"\\/")) + L"\\bridge_diagnostics.log";
    HANDLE file = CreateFileW(dest.c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t{}; GetLocalTime(&t);
    char prefix[128];
    std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu ",
                  t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                  static_cast<unsigned long>(GetCurrentProcessId()));
    std::string text = std::string(prefix) + line + "\r\n";
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

bool sameID(const char* a, const char* b) { return a && b && std::memcmp(a, b, 16) == 0; }
template<class T> bool isID(const char* iid) { return sameID(iid, T::iid); }

struct ClassEntry {
    PClassInfo2 info{};
    TUID originalID{};
    bool component = false;
    bool fix = false;
};
std::vector<ClassEntry> entries;

void mapID(const TUID original, TUID result, int variant) {
    // Stable, separate class IDs prevent collision with installed VSL classes.
    constexpr unsigned char salt[16] = {0x72,0x8d,0xa1,0xc4,0x36,0x5e,0x4a,0x17,
                                       0xa6,0x25,0x1f,0x9b,0x52,0x0c,0x87,0xd3};
    for (int i = 0; i < 16; ++i) result[i] = original[i] ^ salt[i];
    result[15] ^= static_cast<char>(variant);
}

bool loadOriginal() {
    std::call_once(loadOnce, [] {
        originalModule = LoadLibraryExW(originalPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!originalModule) {
            logLine("ERROR LoadLibrary original winerr=" + std::to_string(GetLastError()));
            return;
        }
        using Init = bool (PLUGIN_API*)();
        auto init = reinterpret_cast<Init>(GetProcAddress(originalModule, "InitDll"));
        if (init && !init()) { logLine("ERROR original InitDll failed"); return; }
        using GetFactory = IPluginFactory* (PLUGIN_API*)();
        auto getFactory = reinterpret_cast<GetFactory>(GetProcAddress(originalModule, "GetPluginFactory"));
        if (!getFactory || !(originalFactory = getFactory())) {
            logLine("ERROR original GetPluginFactory failed"); return;
        }
        IPluginFactory2* factory2 = nullptr;
        originalFactory->queryInterface(IPluginFactory2::iid, reinterpret_cast<void**>(&factory2));
        for (int32 i = 0; i < originalFactory->countClasses(); ++i) {
            PClassInfo basic{};
            if (originalFactory->getClassInfo(i, &basic) != kResultOk) continue;
            PClassInfo2 extended{};
            if (!factory2 || factory2->getClassInfo2(i, &extended) != kResultOk) {
                std::memcpy(extended.cid, basic.cid, 16);
                extended.cardinality = basic.cardinality;
                std::strncpy(extended.category, basic.category, sizeof(extended.category) - 1);
                std::strncpy(extended.name, basic.name, sizeof(extended.name) - 1);
            }
            const bool component = std::strcmp(basic.category, kVstAudioEffectClass) == 0;
            // MuseScore 4.7.7 only scans/instantiates the first audio class in
            // each module. Build two files, each exposing one variant.
            for (int count = 0; count < 1; ++count) {
                const int v = component ? BRIDGE_VARIANT_FIX : 0;
                ClassEntry entry;
                entry.info = extended;
                std::memcpy(entry.originalID, basic.cid, 16);
                entry.component = component;
                entry.fix = v == 1;
                mapID(basic.cid, entry.info.cid, component ? (v ? 2 : 1) : 3);
                if (component) {
                    std::snprintf(entry.info.name, sizeof(entry.info.name), "Synchron Offline Bridge %s", v ? "Fix" : "Pass");
                    std::strncpy(entry.info.vendor, "Local Diagnostic Bridge", sizeof(entry.info.vendor) - 1);
                    std::strncpy(entry.info.version, "0.1.0", sizeof(entry.info.version) - 1);
                }
                entries.push_back(entry);
            }
        }
        if (factory2) factory2->release();
        for (size_t i = 0; i < entries.size(); ++i) {
            for (size_t j = i + 1; j < entries.size(); ++j) {
                if (sameID(entries[i].info.cid, entries[j].info.cid)) {
                    logLine("ERROR duplicate mapped class IDs"); entries.clear(); return;
                }
            }
        }
        logLine("loaded original at original absolute path; exposedClasses=" + std::to_string(entries.size()));
    });
    return originalFactory != nullptr && !entries.empty();
}

class ComponentProxy final : public IComponent, public IAudioProcessor {
public:
    ComponentProxy(IComponent* component, bool fix)
        : component_(component), fix_(fix), id_(nextInstance.fetch_add(1)) {
        component_->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&processor_));
        logLine(label() + "created processor=" + std::to_string(processor_ != nullptr));
    }
    ~ComponentProxy() {
        flushEvents("destroy");
        logLine(label() + "destroy calls=" + std::to_string(calls_) +
                " mismatchCalls=" + std::to_string(mismatchCalls_) + " corrected=" + std::to_string(correctedCalls_));
        if (processor_) processor_->release();
        component_->release();
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (isID<FUnknown>(iid) || isID<IPluginBase>(iid) || isID<IComponent>(iid))
            *obj = static_cast<IComponent*>(this);
        else if (isID<IAudioProcessor>(iid) && processor_)
            *obj = static_cast<IAudioProcessor*>(this);
        else
            return component_->queryInterface(iid, obj);
        addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs_; }
    uint32 PLUGIN_API release() override {
        const uint32 count = --refs_; if (!count) delete this; return count;
    }
    tresult PLUGIN_API initialize(FUnknown* context) override { return component_->initialize(context); }
    tresult PLUGIN_API terminate() override { flushEvents("terminate"); return component_->terminate(); }
    tresult PLUGIN_API getControllerClassId(TUID cid) override {
        TUID original{};
        tresult result = component_->getControllerClassId(original);
        if (result == kResultOk) {
            bool mapped = false;
            for (const auto& entry : entries) {
                if (!entry.component && sameID(entry.originalID, original)) {
                    std::memcpy(cid, entry.info.cid, 16); mapped = true; break;
                }
            }
            if (!mapped) {
                logLine(label() + "ERROR controller class ID is not exposed by original factory");
                return kNoInterface;
            }
        }
        return result;
    }
    tresult PLUGIN_API setIoMode(IoMode mode) override { return component_->setIoMode(mode); }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override { return component_->getBusCount(type, dir); }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override {
        return component_->getBusInfo(type, dir, index, bus);
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo& input, RoutingInfo& output) override {
        return component_->getRoutingInfo(input, output);
    }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) override {
        return component_->activateBus(type, dir, index, state);
    }
    tresult PLUGIN_API setActive(TBool state) override { return component_->setActive(state); }
    tresult PLUGIN_API setState(IBStream* state) override { return component_->setState(state); }
    tresult PLUGIN_API getState(IBStream* state) override { return component_->getState(state); }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in, int32 ni, SpeakerArrangement* out, int32 no) override {
        return processor_ ? processor_->setBusArrangements(in, ni, out, no) : kNotImplemented;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement& arr) override {
        return processor_ ? processor_->getBusArrangement(dir, index, arr) : kNotImplemented;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override {
        return processor_ ? processor_->canProcessSampleSize(size) : kNotImplemented;
    }
    uint32 PLUGIN_API getLatencySamples() override { return processor_ ? processor_->getLatencySamples() : 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if (!processor_) return kNotImplemented;
        tresult result = processor_->setupProcessing(setup);
        if (result == kResultOk) {
            setupMode_ = setup.processMode;
            maxSamples_ = setup.maxSamplesPerBlock;
            sampleRate_ = setup.sampleRate;
            logNextProcess_ = true;
        }
        logLine(label() + "setup mode=" + std::to_string(setup.processMode) +
                " maxSamples=" + std::to_string(setup.maxSamplesPerBlock) +
                " sampleRate=" + std::to_string(setup.sampleRate) + " result=" + std::to_string(result));
        return result;
    }
    tresult PLUGIN_API setProcessing(TBool state) override {
        const tresult result = processor_ ? processor_->setProcessing(state) : kNotImplemented;
        if (!state) flushEvents("setProcessing(false)");
        return result;
    }
    tresult PLUGIN_API process(ProcessData& data) override {
        if (!processor_) return kNotImplemented;
        const int32 incoming = data.processMode;
        const int32 actual = fix_ ? setupMode_ : incoming;
        ++calls_;
        if (incoming != setupMode_) ++mismatchCalls_;
        if (incoming != actual) ++correctedCalls_;
        // Fixed memory only in the audio callback. No per-event disk writes.
        if (data.inputEvents && data.processContext && sampleRate_ > 0) {
            const int64 position = data.processContext->projectTimeSamples;
            for (int32 i = 0; i < data.inputEvents->getEventCount(); ++i) {
                Event event{};
                if (data.inputEvents->getEvent(i, event) != kResultOk) continue;
                if (event.type != Event::kNoteOnEvent && event.type != Event::kNoteOffEvent) continue;
                const int64 absolute = position + event.sampleOffset;
                if (absolute < static_cast<int64>(21.5 * sampleRate_) || absolute > static_cast<int64>(24.5 * sampleRate_)) continue;
                if (eventCount_ >= eventRecords_.size()) { ++eventsDropped_; continue; }
                auto& record = eventRecords_[eventCount_++];
                record.project = position; record.offset = event.sampleOffset;
                record.numSamples = data.numSamples; record.type = event.type;
                record.pitch = event.type == Event::kNoteOnEvent ? event.noteOn.pitch : event.noteOff.pitch;
                record.noteID = event.type == Event::kNoteOnEvent ? event.noteOn.noteId : event.noteOff.noteId;
                record.channel = event.type == Event::kNoteOnEvent ? event.noteOn.channel : event.noteOff.channel;
                record.incoming = incoming; record.actual = actual;
                record.sampleRate = sampleRate_;
            }
        }
        if (logNextProcess_ || incoming != lastIncoming_ || actual != lastActual_) {
            if (modeCount_ < modeRecords_.size()) {
                modeRecords_[modeCount_++] = {incoming, actual, setupMode_, maxSamples_, data.numSamples};
            }
            logNextProcess_ = false; lastIncoming_ = incoming; lastActual_ = actual;
        }
        data.processMode = actual;
        const tresult result = processor_->process(data);
        data.processMode = incoming;
        return result;
    }
    uint32 PLUGIN_API getTailSamples() override { return processor_ ? processor_->getTailSamples() : 0; }
private:
    struct EventRecord {
        int64 project = 0;
        int32 offset = 0, numSamples = 0, type = 0, pitch = 0, noteID = 0, channel = 0;
        int32 incoming = 0, actual = 0;
        double sampleRate = 0;
    };
    struct ModeRecord { int32 incoming, actual, setup, maximum, count; };
    void flushEvents(const char* reason) {
        if (!eventCount_ && !eventsDropped_ && !modeCount_) return;
        std::string lines = label() + "event_trace reason=" + reason + " count=" + std::to_string(eventCount_) +
                            " dropped=" + std::to_string(eventsDropped_);
        for (size_t i = 0; i < modeCount_; ++i) {
            const auto& r = modeRecords_[i];
            lines += "\r\n" + label() + "process incomingMode=" + std::to_string(r.incoming) +
                     " actualMode=" + std::to_string(r.actual) + " setupMode=" + std::to_string(r.setup) +
                     " maxSamples=" + std::to_string(r.maximum) + " numSamples=" + std::to_string(r.count);
        }
        for (size_t i = 0; i < eventCount_; ++i) {
            const auto& r = eventRecords_[i];
            lines += "\r\n" + label() + "event project=" + std::to_string(r.project) + " sampleOffset=" + std::to_string(r.offset) +
                     " absoluteSeconds=" + std::to_string((r.project + r.offset) / r.sampleRate) +
                     " type=" + std::to_string(r.type) + " pitch=" + std::to_string(r.pitch) +
                     " noteId=" + std::to_string(r.noteID) + " channel=" + std::to_string(r.channel) +
                     " numSamples=" + std::to_string(r.numSamples) + " incomingMode=" + std::to_string(r.incoming) +
                     " actualMode=" + std::to_string(r.actual);
        }
        logLine(lines);
        eventCount_ = 0; eventsDropped_ = 0; modeCount_ = 0;
    }
    std::string label() const { return "instance=" + std::to_string(id_) + (fix_ ? " Fix " : " Pass "); }
    std::atomic<uint32> refs_{1};
    IComponent* component_ = nullptr;
    IAudioProcessor* processor_ = nullptr;
    bool fix_ = false;
    unsigned long id_ = 0;
    int32 setupMode_ = kRealtime, maxSamples_ = 0, lastIncoming_ = -1, lastActual_ = -1;
    bool logNextProcess_ = true;
    uint64_t calls_ = 0, mismatchCalls_ = 0, correctedCalls_ = 0;
    double sampleRate_ = 0;
    std::array<EventRecord, 256> eventRecords_{};
    size_t eventCount_ = 0, eventsDropped_ = 0;
    std::array<ModeRecord, 64> modeRecords_{};
    size_t modeCount_ = 0;
};

class FactoryProxy final : public IPluginFactory3 {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (isID<FUnknown>(iid) || isID<IPluginFactory>(iid) || isID<IPluginFactory2>(iid) || isID<IPluginFactory3>(iid)) {
            *obj = static_cast<IPluginFactory3*>(this); addRef(); return kResultOk;
        }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs_; }
    uint32 PLUGIN_API release() override {
        const uint32 count = --refs_; if (!count) delete this; return count;
    }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override {
        if (!info || !loadOriginal()) return kInvalidArgument;
        tresult result = originalFactory->getFactoryInfo(info);
        if (result == kResultOk) std::strncpy(info->vendor, "Local Diagnostic Bridge", sizeof(info->vendor) - 1);
        return result;
    }
    int32 PLUGIN_API countClasses() override { return loadOriginal() ? static_cast<int32>(entries.size()) : 0; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override {
        if (!info || !valid(index)) return kInvalidArgument;
        const auto& src = entries[index].info;
        std::memcpy(info->cid, src.cid, 16); info->cardinality = src.cardinality;
        std::memcpy(info->category, src.category, sizeof(info->category));
        std::memcpy(info->name, src.name, sizeof(info->name)); return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override {
        if (!info || !valid(index)) return kInvalidArgument;
        *info = entries[index].info; return kResultOk;
    }
    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override {
        if (!info || !valid(index)) return kInvalidArgument;
        const auto& src = entries[index].info;
        std::memset(info, 0, sizeof(*info));
        std::memcpy(info->cid, src.cid, 16); info->cardinality = src.cardinality;
        std::memcpy(info->category, src.category, sizeof(info->category));
        info->classFlags = src.classFlags;
        std::memcpy(info->subCategories, src.subCategories, sizeof(info->subCategories));
        copyWide(info->name, src.name); copyWide(info->vendor, src.vendor);
        copyWide(info->version, src.version); copyWide(info->sdkVersion, src.sdkVersion);
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override {
        if (!obj || !loadOriginal()) return kInvalidArgument;
        *obj = nullptr;
        const ClassEntry* entry = nullptr;
        for (const auto& candidate : entries) if (sameID(candidate.info.cid, cid)) { entry = &candidate; break; }
        if (!entry) return kInvalidArgument;
        if (!entry->component) return originalFactory->createInstance(entry->originalID, iid, obj);
        IComponent* component = nullptr;
        const tresult result = originalFactory->createInstance(entry->originalID, IComponent::iid,
                                                              reinterpret_cast<void**>(&component));
        if (result != kResultOk || !component) return result;
        auto proxy = new ComponentProxy(component, entry->fix);
        const tresult queried = proxy->queryInterface(iid, obj);
        proxy->release();
        return queried;
    }
    tresult PLUGIN_API setHostContext(FUnknown* context) override {
        if (!loadOriginal()) return kNotInitialized;
        IPluginFactory3* factory3 = nullptr;
        if (originalFactory->queryInterface(IPluginFactory3::iid, reinterpret_cast<void**>(&factory3)) != kResultOk)
            return kNotImplemented;
        const tresult result = factory3->setHostContext(context); factory3->release(); return result;
    }
private:
    static bool valid(int32 index) { return loadOriginal() && index >= 0 && index < static_cast<int32>(entries.size()); }
    template<size_t N> static void copyWide(char16 (&dest)[N], const char* src) {
        for (size_t i = 0; i < N - 1 && src[i]; ++i) dest[i] = static_cast<unsigned char>(src[i]);
    }
    std::atomic<uint32> refs_{1};
};
} // namespace

extern "C" __declspec(dllexport) IPluginFactory* PLUGIN_API GetPluginFactory() {
    if (!loadOriginal()) return nullptr;
    return new FactoryProxy();
}
extern "C" __declspec(dllexport) bool PLUGIN_API InitDll() { return true; }
extern "C" __declspec(dllexport) bool PLUGIN_API ExitDll() {
    // Keep original module pinned until process exit. It may also be loaded by
    // the host directly; unloading here could invalidate shared VSL resources.
    return true;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { selfModule = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
