#pragma once

#include <atomic>
#include <cstdint>
#include "toolbox/sys/Error.hpp"
#include <toolbox/io/Hook.hpp>
#include <toolbox/io/Waker.hpp>
#include <toolbox/io/Timer.hpp>
#include <toolbox/io/State.hpp>
#include <toolbox/io/Handle.hpp>

namespace toolbox {
inline namespace io {

constexpr Duration NoTimeout{-1};
enum class Priority { High = 0, Low = 1 };

// event loop base
class Scheduler {
public:
    using This = Scheduler;
    using FD = os::FD;
    

public:
    Scheduler() = default;

    // Copy.
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Move.
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    /// Throws std::bad_alloc only.
    [[nodiscard]] Timer timer(MonoTime expiry, Duration interval, Priority priority, TimerSlot slot) {
        return tqs_[static_cast<size_t>(priority)].insert(expiry, interval, slot);
    }
    /// Throws std::bad_alloc only.
    [[nodiscard]] Timer timer(MonoTime expiry, Priority priority, TimerSlot slot) {
        return tqs_[static_cast<size_t>(priority)].insert(expiry, slot);
    }
    
    // clang-format on
    void add_hook(Hook& hook) noexcept { hooks_.push_back(hook); }
    

    void stop() {
      if(state()!=State::Stopping && state()!=State::Stopped) {
          state(State::Stopping);
          stop_.store(true, std::memory_order_release);
      }
    }
    
    MonoTime next_expiry(MonoTime next) const {
        enum { High = 0, Low = 1 };
        using namespace std::chrono;
        {
            auto& tq = tqs_[High];
            if (!tq.empty()) {
                // Duration until next expiry. Mitigate scheduler latency by preempting the
                // high-priority timer and busy-waiting for 200us ahead of timer expiry.
                next = min(next, tq.front().expiry() - 200us);
            }
        }
        {
            auto& tq = tqs_[Low];
            if (!tq.empty()) {
                // Duration until next expiry.
                next = min(next, tq.front().expiry());
            }
        }
        return next;
    }

    
    auto& state_changed() noexcept { return state_changed_; }
    State state() const noexcept { return state_; }
    void state(State val) noexcept { state_.store(val, std::memory_order_release); state_changed().invoke(this, val); }

protected:
    static_assert(static_cast<int>(Priority::High) == 0);
    static_assert(static_cast<int>(Priority::Low) == 1);
    HookList hooks_;
    std::atomic<bool> stop_{false};
    Signal<Scheduler*, State> state_changed_;
    std::atomic<State> state_{State::Stopped};
    TimerPool tp_;
    std::array<TimerQueue, 2> tqs_{tp_, tp_};
};

}// io
}// toolbox