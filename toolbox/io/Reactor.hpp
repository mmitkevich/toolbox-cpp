// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
// Copyright (C) 2020 Reactive Markets Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TOOLBOX_IO_REACTOR_HPP
#define TOOLBOX_IO_REACTOR_HPP
#include <atomic>
#include <toolbox/io/Epoll.hpp>
#include <toolbox/io/EventFd.hpp>
#include <toolbox/io/Hook.hpp>
#include <toolbox/io/Waker.hpp>
#include <toolbox/io/Timer.hpp>
#include <toolbox/io/State.hpp>
#include <toolbox/io/Handle.hpp>
#include <toolbox/io/Runner.hpp>

namespace toolbox {
inline namespace io {

constexpr Duration NoTimeout{-1};
enum class Priority { High = 0, Low = 1 };




template<typename PollerT>
class BasicReactor : public Waker {
public:
    //using Event = EpollEvent;
    using Handle = Epoll::Handle;
    using FD = typename PollerT::FD;

    using StateChangedSignal = toolbox::util::Signal<BasicReactor*, State>;
    

    template<typename...ArgsT>
    explicit BasicReactor(ArgsT...args)
    : poller_(std::forward<ArgsT>(args)...)
    {}

    ~BasicReactor() {}

    // Copy.
    BasicReactor(const BasicReactor&) = delete;
    BasicReactor& operator=(const BasicReactor&) = delete;

    // Move.
    BasicReactor(BasicReactor&&) = delete;
    BasicReactor& operator=(BasicReactor&&) = delete;

    PollerT& poller() { return poller_; }
    
    /// Throws std::bad_alloc only.
    [[nodiscard]] Timer timer(MonoTime expiry, Duration interval, Priority priority, TimerSlot slot) {
        return tqs_[static_cast<size_t>(priority)].insert(expiry, interval, slot);
    }
    /// Throws std::bad_alloc only.
    [[nodiscard]] Timer timer(MonoTime expiry, Priority priority, TimerSlot slot) {
        return tqs_[static_cast<size_t>(priority)].insert(expiry, slot);
    }

    // clang-format off
    [[nodiscard]] 
    Handle subscribe(FD fd, IoEvent events, IoSlot slot) {
      return poller_.subscribe(fd, events, slot);
    }
  
    int poll(CyclTime now, Duration timeout = NoTimeout);

    // clang-format on
    void add_hook(Hook& hook) noexcept { hooks_.push_back(hook); }
    
    static constexpr long BusyWaitCycles{100};
    void run(std::size_t busy_cycles = BusyWaitCycles);
    void stop() {
      if(state()!=State::Stopping && state()!=State::Stopped) {
          state(State::Stopping);
          stop_.store(true, std::memory_order_release);
      }
    }
    StateChangedSignal& state_changed() noexcept { return state_changed_; }
    State state() const noexcept { return state_; }
    void state(State val) noexcept { state_.store(val, std::memory_order_release); state_changed().invoke(this, val); }
protected:
    /// Thread-safe.
    void do_wakeup() noexcept final { poller_.do_wakeup(); }

private:
    MonoTime next_expiry(MonoTime next) const;
    
    PollerT poller_;

    static_assert(static_cast<int>(Priority::High) == 0);
    static_assert(static_cast<int>(Priority::Low) == 1);

    TimerPool tp_;
    std::array<TimerQueue, 2> tqs_{tp_, tp_};
    HookList hooks_;
    std::atomic<bool> stop_{false};
    StateChangedSignal state_changed_;
    std::atomic<State> state_{State::Stopped};
};

template<typename PollerT>
inline int BasicReactor<PollerT>::poll(CyclTime now, Duration timeout)
{
    enum { High = 0, Low = 1 };
    using namespace std::chrono;

    // If timeout is zero then the wait_until time should also be zero to signify no wait.
    MonoTime wait_until{};
    if (!is_zero(timeout) && hooks_.empty()) {
        const MonoTime next
            = next_expiry(timeout == NoTimeout ? MonoClock::max() : now.mono_time() + timeout);
        if (next > now.mono_time()) {
            wait_until = next;
        }
    }

    int n;
    std::error_code ec;
    if (wait_until < MonoClock::max()) {
        // The wait function will not block if time is zero.
        n = poller_.poll(wait_until, ec);
    } else {
        // Block indefinitely.
        n = poller_.poll(ec);
    }
    if (ec) {
        if (ec.value() != EINTR) {
            throw std::system_error{ec};
        }
        return 0;
    }
    // If the epoller call was a blocking call, then acquire the current time.
    if (!is_zero(wait_until)) {
        now = CyclTime::now();
    }
    n = tqs_[High].dispatch(now) + poller_.dispatch(now);
    // Low priority timers are only dispatched during empty cycles.
    if (n == 0) {
        n += tqs_[Low].dispatch(now);
    }
    io::dispatch(now, hooks_);
    return n;
}

template<typename PollerT>
void BasicReactor<PollerT>::run(std::size_t busy_cycles)
{
    state(State::Starting);
    state(State::Started);
    std::size_t i{0};
    while (!stop_.load(std::memory_order_acquire)) {
        // Busy-wait for a small number of cycles after work was done.
        if (poll(CyclTime::now(), i++ < busy_cycles ? 0s : NoTimeout) > 0) {
            // Reset counter when work has been done.
            i = 0;
        }
    }
    state(State::Stopping);
    state(State::Stopped);
}

template<typename PollerT>
inline MonoTime BasicReactor<PollerT>::next_expiry(MonoTime next) const
{
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

using Reactor = BasicReactor<Epoll>;
using ReactorRunner = BasicRunner<Reactor>;

} // namespace io
} // namespace toolbox

#endif // TOOLBOX_IO_REACTOR_HPP
