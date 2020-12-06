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

#pragma once

#include <cstdint>
#include <utility>

#include <toolbox/io/Scheduler.hpp>
#include <toolbox/sys/Error.hpp>

#include "toolbox/io/Handle.hpp"
#include "toolbox/io/PollHandle.hpp"

#include <toolbox/io/Epoll.hpp>
#include <toolbox/io/Qpoll.hpp>

#include <toolbox/io/EventFd.hpp>

#include <toolbox/io/Runner.hpp>

#include <toolbox/util/Tuple.hpp>

namespace toolbox {
inline namespace io {

class IReactor : public IWaker {
public:
    //virtual ~IReactor() = default;
    /// create timers
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual Timer timer(MonoTime expiry, Duration interval, Priority priority, TimerSlot slot) = 0;
    virtual Timer timer(MonoTime expiry, Priority priority, TimerSlot slot) = 0;
    
    /// get poll function for FD
    virtual PollSlot ctl(os::FD fd) = 0;
    PollHandle poll(os::FD fd) {
        return PollHandle {fd, ctl(fd)};
    }
};

template<typename ImplT>
class ReactorImpl : public IReactor {
public:
    template<typename...ArgsT>
    explicit ReactorImpl(ArgsT...args)
    : impl_(std::forward<ArgsT>(args)...) {}

    void run() final { impl_.run(); }
    void stop() final { impl_.stop();}
    Timer timer(MonoTime expiry, Duration interval, Priority priority, TimerSlot slot) final {
        return impl_.timer(expiry, interval, priority, slot);
    }
    Timer timer(MonoTime expiry, Priority priority, TimerSlot slot) final {
        return impl_.timer(expiry, priority, slot);
    }
    PollSlot ctl(os::FD fd) final {
        return impl_.ctl(fd);
    }
    void wakeup() noexcept final {
        impl_.wakeup();
    }
private:
    ImplT impl_;
};
class PollBase {
public:
    PollBase();
private:
    PollSlot ctl_;
};
/// multi-reactor
template<typename...ImplsT>
class BasicReactor : public Scheduler
{
public:
    using This = BasicReactor<ImplsT...>;
    using Base = Scheduler;
    using Handle = PollHandle;
    using Runner = BasicRunner<This>;
    using FD = os::FD;

    constexpr static std::size_t ImplsSize = sizeof...(ImplsT);
    static_assert(ImplsSize>0, "at least one implementation required");

    static constexpr unsigned CustomFDBits = 12;            // per reactor
    static constexpr unsigned CustomFDMask = (1U<<12)-1;    // per reactor
    static constexpr FD HighBitFDMask = (1U<<31);           // high bit means custom fd
public:
    using Base::Base;
    using Base::timers, Base::hooks, Base::next_expiry;

    template<typename...ArgsT>
    explicit BasicReactor(ArgsT...args)
    : impls_(std::forward<ArgsT>(args)...)                                        // data
    {}

    /// return poll ctl function from file descriptor
    PollSlot ctl(FD fd) {
        std::size_t ix = fd & (~CustomFDMask);
        std::size_t rix = ix >> CustomFDBits;
        if((fd & HighBitFDMask)==0) {
            rix = ImplsSize-1;
        }
        PollSlot ctl;
        tuple_for_each(impls_, [&ctl, &rix](auto &impl) {
            if(rix==0) {
                using P = std::decay_t<decltype(impl)>;
                ctl = util::bind<&P::ctl>(&impl);
            }
            rix--;
        });
        return ctl;
    }

    // get poll handle
    PollHandle poll(FD fd) {
        return PollHandle{fd, ctl(fd)};
    }

    /// event loop cycle
    void run() {
        state(State::Starting);
        state(State::Started);
        std::size_t i {0};
        while (!Scheduler::stop_.load(std::memory_order_acquire)) {
            // Busy-wait for a small number of cycles after work was done.
            if (poll(CyclTime::now(), i++ < 100 ? 0s : NoTimeout) > 0) {
                // Reset counter when work has been done.
                i = 0;
            }
        }
        state(State::Stopping);
        state(State::Stopped);
    }

    /// busy poll for events
    int poll(CyclTime now, Duration timeout = NoTimeout)
    {
        using namespace std::chrono;

        // If timeout is zero then the wait_until time should also be zero to signify no wait.
        MonoTime wait_until{};
        if (!is_zero(timeout) && hooks().empty()) {
            const MonoTime next = next_expiry(timeout == NoTimeout ? MonoClock::max() : now.mono_time() + timeout);
            if (next > now.mono_time()) {
                wait_until = next;
            }
        }
        int rn = 0;
        std::size_t i = ImplsSize;
        tuple_for_each(impls_, [&](auto &r) {
            i--;
            int n = 0;
            std::error_code ec;
            if(i>0) {
                n = r.wait(MonoTime{}, ec); // no blocking on pollers except last one
            } else if (wait_until < MonoClock::max()) {
                // The wait function will not block if time is zero.
                n = r.wait(wait_until, ec);
            } else {
                // Block indefinitely.
                n = r.wait(ec);
            }
            if (ec) {
                if (ec.value() != EINTR) {
                    throw std::system_error{ec};
                }
            } else {
                // If the epoller call was a blocking call, then acquire the current time.
                if (!is_zero(wait_until)) {
                    now = CyclTime::now();
                }
                rn += timers(Priority::High).dispatch(now) + r.dispatch(now);
                // Low priority timers are only dispatched during empty cycles.
                if (i==0 && n == 0) {
                    rn += timers(Priority::Low).dispatch(now);
                }
            }
        });
        io::dispatch(now, hooks());
        return rn;
    }

    /// wakeup, could be called from another thread
    void wakeup() noexcept  { 
        // wakeup last reactor since others are always busy-polled
        std::get<ImplsSize-1>(impls_).wakeup(); 
    }
protected:
    std::tuple<ImplsT...> impls_;
};

using Reactor = BasicReactor<Epoll>;


} // namespace io
} // namespace toolbox

