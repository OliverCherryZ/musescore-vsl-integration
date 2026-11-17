#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include "async/async.h"
#include "async/asyncable.h"
#include "async/processevents.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
static int checks = 0;
static void require(bool ok, const char* text) {
    ++checks;
    if (!ok) throw std::runtime_error(text);
}

struct Observation {
    std::atomic<int> polls{0}, rawTimeouts{0}, destroyed{0}, timersDestroyed{0};
    std::atomic<bool> disconnected{false}, pollHeldOwner{false}, releaseCompleted{false};
    std::thread::id destroyedOn;
    std::function<void()> onPoll;
};

// Uses the exact callback statement extracted from the production .cpp by the
// runner, actual Qt timers, and the production kors Async main-thread queue.
// No VST, MuseScore app, sample loader or plug-in factory is instantiated.
class PollOwner final : public kors::async::Asyncable,
                        public std::enable_shared_from_this<PollOwner> {
public:
    explicit PollOwner(std::shared_ptr<Observation> seen)
        : stats(std::move(seen)), mainThread(std::this_thread::get_id()) {}
    ~PollOwner() override {
        stats->disconnected = !async_isConnected();
        stats->destroyedOn = std::this_thread::get_id();
        ++stats->destroyed;
        QTimer* preparationTimer = m_vslPreparationTimer;
        m_vslPreparationTimer = nullptr;
        kors::async::Async::call(nullptr, [preparationTimer]() {
            if (preparationTimer) {
                preparationTimer->stop();
                delete preparationTimer;
            }
        }, mainThread);
    }
    void start() {
        m_vslPreparationTimer = new QTimer;
        m_vslPreparationTimer->setInterval(1);
#include "production_timer_connection.inc"
        const auto seen = stats;
        QObject::connect(m_vslPreparationTimer, &QTimer::timeout, m_vslPreparationTimer,
                         [seen]() { ++seen->rawTimeouts; });
        QObject::connect(m_vslPreparationTimer, &QObject::destroyed,
                         [seen]() { ++seen->timersDestroyed; });
        m_vslPreparationTimer->start();
    }
    QTimer* timer() const { return m_vslPreparationTimer; }
private:
    void pollVslPreparation() {
        ++stats->polls;
        if (stats->onPoll) stats->onPoll();
    }
    std::shared_ptr<Observation> stats;
    std::thread::id mainThread;
    QTimer* m_vslPreparationTimer = nullptr;
};

static void qtOnly(int milliseconds) {
    QElapsedTimer elapsed; elapsed.start();
    do { QCoreApplication::processEvents(); QThread::msleep(1); }
    while (elapsed.elapsed() < milliseconds);
}
static void cleanup() { kors::async::processMessages(); QCoreApplication::processEvents(); }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        { // Normal periodic operation and a same-thread final release.
            auto s = std::make_shared<Observation>();
            auto p = std::make_shared<PollOwner>(s); std::weak_ptr<PollOwner> weak = p;
            p->start(); qtOnly(12);
            require(s->polls >= 2, "normal timer did not poll");
            p.reset();
            require(weak.expired(), "timer created an owner cycle");
            cleanup();
            require(s->destroyed == 1 && s->timersDestroyed == 1, "normal cleanup count");
            require(s->disconnected, "timer registered an Asyncable notification receiver");
        }
        { // Audio-thread release, deliberately keep the Qt timer alive before cleanup.
            auto s = std::make_shared<Observation>();
            auto p = std::make_shared<PollOwner>(s); std::weak_ptr<PollOwner> weak = p;
            p->start(); qtOnly(5);
            const int polls = s->polls, raw = s->rawTimeouts;
            QMetaObject::invokeMethod(p->timer(), "timeout", Qt::QueuedConnection);
            std::thread worker([owned = std::move(p)]() mutable { owned.reset(); }); worker.join();
            require(weak.expired() && s->destroyed == 1, "worker release retained owner");
            qtOnly(12);
            require(s->rawTimeouts > raw, "expired-owner test delivered no Qt timeout");
            require(s->polls == polls, "timeout entered a destroyed owner");
            cleanup();
            require(s->timersDestroyed == 1 && s->disconnected, "worker cleanup retains notification state");
        }
        { // Final external reference disappears while a real timer callback is running.
            auto s = std::make_shared<Observation>();
            auto p = std::make_shared<PollOwner>(s); std::weak_ptr<PollOwner> weak = p;
            std::promise<void> entered, released;
            auto enteredFuture = entered.get_future();
            auto releasedFuture = released.get_future();
            s->onPoll = [s, weak, &entered, &releasedFuture]() {
                entered.set_value();
                s->releaseCompleted = releasedFuture.wait_for(2s) == std::future_status::ready;
                s->pollHeldOwner = !weak.expired() && s->destroyed == 0;
            };
            p->start();
            std::thread worker([owned = std::move(p), &enteredFuture, &released]() mutable {
                enteredFuture.wait_for(2s); owned.reset(); released.set_value();
            });
            qtOnly(15); worker.join();
            require(s->releaseCompleted && s->pollHeldOwner, "callback did not pin owner during concurrent release");
            require(weak.expired() && s->destroyed == 1, "callback leaked its strong owner");
            require(s->destroyedOn == std::this_thread::get_id(), "last callback release did not happen on main thread");
            s->onPoll = {}; // release the test observer's deliberate self capture
            cleanup();
            require(s->timersDestroyed == 1, "concurrent callback timer not cleaned");
        }
        { // Context destruction removes an already posted Qt signal safely.
            auto s = std::make_shared<Observation>();
            auto p = std::make_shared<PollOwner>(s);
            p->start();
            QMetaObject::invokeMethod(p->timer(), "timeout", Qt::QueuedConnection);
            p.reset();
            kors::async::processMessages();
            require(s->timersDestroyed == 1, "context not destroyed before event delivery");
            qtOnly(5);
            require(s->polls == 0 && s->rawTimeouts == 0, "deleted timer delivered queued callback");
        }
        for (int i = 0; i < 32; ++i) {
            auto s = std::make_shared<Observation>();
            auto p = std::make_shared<PollOwner>(s); std::weak_ptr<PollOwner> weak = p;
            p->start();
            QMetaObject::invokeMethod(p->timer(), "timeout", Qt::QueuedConnection);
            std::thread worker([owned = std::move(p)]() mutable { owned.reset(); }); worker.join();
            qtOnly(2); cleanup();
            require(weak.expired() && s->destroyed == 1 && s->timersDestroyed == 1 && s->polls == 0 && s->disconnected,
                    "repeated worker-release lifecycle failed");
        }
        std::cout << "PASS assertions=" << checks << " realQtTimer=true actualAsyncQueue=true repeatedWorkerRelease=32\n";
        kors::async::terminate();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        kors::async::terminate();
        return 1;
    }
}
