#include "../../src/src/framework/vst/internal/vstpluginlifetime.h"

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Observation {
    bool moduleAlive = false;
    int providersDestroyed = 0;
    int modulesDestroyed = 0;
    int unsafeProviderDestructions = 0;
    std::vector<std::string> events;
};
struct Module {
    Observation& o;
    explicit Module(Observation& value) : o(value) { o.moduleAlive = true; }
    ~Module() { o.moduleAlive = false; ++o.modulesDestroyed; o.events.push_back("module"); }
};
struct Provider {
    Observation& o;
    explicit Provider(Observation& value) : o(value) {}
    ~Provider() {
        if (!o.moduleAlive) ++o.unsafeProviderDestructions;
        ++o.providersDestroyed; o.events.push_back("provider");
    }
    void deactivate() { o.events.push_back("deactivate"); }
};
using ModulePtr = std::shared_ptr<Module>;
using ProviderPtr = std::unique_ptr<Provider>;
using Owner = muse::vst::detail::VstPluginLifetime<ModulePtr, ProviderPtr>;

static int assertions = 0;
static void check(bool value, const char* what) {
    ++assertions;
    if (!value) throw std::runtime_error(what);
}
static std::function<void()> callback(Observation& o, ModulePtr module = {}) {
    if (!module) module = std::make_shared<Module>(o);
    auto owner = std::make_shared<Owner>(std::move(module), std::make_unique<Provider>(o));
    return [owner] {
        if (owner->provider()) owner->provider()->deactivate();
        owner->reset();
    };
}
static void assertFinal(const Observation& o, int count = 1) {
    check(o.unsafeProviderDestructions == 0, "provider outlived module");
    check(o.providersDestroyed == count, "provider destruction count");
    check(o.modulesDestroyed == 1, "module destruction count");
}
int main() {
    try {
        { // Normal callback execution, repeated reset, then closure destruction.
            Observation o;
            auto f = callback(o); f(); f(); f = {};
            assertFinal(o);
            check(o.events == std::vector<std::string>{"deactivate", "provider", "module"}, "normal order");
        }
        { // Queue destruction without invoking callback: no VST/UI/process runs.
            Observation o;
            std::vector<std::function<void()>> queue;
            queue.push_back(callback(o)); queue.clear();
            assertFinal(o);
            check(o.events == std::vector<std::string>{"provider", "module"}, "discard order");
        }
        { // std::function copies in Async::callQueue retain the same owner.
            Observation o;
            auto f = callback(o); auto copy = f; f = {};
            check(o.providersDestroyed == 0 && o.moduleAlive, "copy keeps owner");
            copy = {}; assertFinal(o);
        }
        { // Mixed executed/discarded callbacks sharing one module.
            Observation o;
            auto module = std::make_shared<Module>(o);
            std::vector<std::function<void()>> queue;
            for (int i = 0; i < 4; ++i) queue.push_back(callback(o, module));
            module.reset(); queue[0](); queue[2]();
            check(o.moduleAlive && o.providersDestroyed == 2, "remaining callbacks pin module");
            queue.clear(); assertFinal(o, 4);
            check(o.events.back() == "module", "shared module destroyed last");
        }
        { // Exception before explicit cleanup still releases in safe order.
            Observation o;
            auto module = std::make_shared<Module>(o);
            auto owner = std::make_shared<Owner>(std::move(module), std::make_unique<Provider>(o));
            std::function<void()> f = [owner] { owner->provider()->deactivate(); throw 1; };
            owner.reset(); try { f(); } catch (int) {} f = {};
            assertFinal(o);
        }
        { // Null/partially initialized provider remains safe.
            Observation o;
            auto owner = std::make_shared<Owner>(std::make_shared<Module>(o), ProviderPtr{});
            owner->reset(); owner.reset();
            check(o.modulesDestroyed == 1 && o.providersDestroyed == 0, "empty provider");
        }
        { // Non-crashing diagnostic for the old independent-capture pattern.
            Observation o;
            auto provider = std::make_shared<Provider>(o);
            auto module = std::make_shared<Module>(o);
            std::function<void()> f = [provider, module]() mutable { provider.reset(); module.reset(); };
            provider.reset(); module.reset(); f = {};
            std::cout << "legacyIndependentCaptureUnsafe=" << o.unsafeProviderDestructions << '\n';
            // Capture member order is implementation-dependent; the production
            // MSVC disassembly, not portable C++ rules, proves its old order.
        }
        std::cout << "PASS assertions=" << assertions << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n'; return 1;
    }
}
