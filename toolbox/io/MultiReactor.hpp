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

#include <boost/mp11/algorithm.hpp>

#include <toolbox/io/Reactor.hpp>
#include <toolbox/sys/Error.hpp>

#include "toolbox/io/Handle.hpp"
#include "toolbox/io/Reactor.hpp"

#include <toolbox/io/Epoll.hpp>
#include <toolbox/io/Qpoll.hpp>

#include <toolbox/io/EventFd.hpp>

#include <toolbox/io/Runner.hpp>

#include <toolbox/util/Tuple.hpp>

namespace toolbox {
inline namespace io {

/// polls multiple implementations
template<typename...ImplsT>
class BasicMultiReactor : public Reactor
{
public:
    using Self = BasicMultiReactor<ImplsT...>;
    using Base = Reactor;
    using Handle = PollHandle;
    using ImplsTuple = std::tuple<ImplsT...>;
    constexpr static std::size_t ImplsSize = sizeof...(ImplsT);
    static_assert(ImplsSize>0, "at least one implementation required");

    static constexpr unsigned CustomFDBits = 12;            // per reactor
    static constexpr unsigned CustomFDMask = (1U<<12)-1;    // per reactor
    static constexpr int HighBitFDMask = (1U<<31);           // high bit means custom fd
public:
    using Base::Base;
    using Base::timers, Base::hooks, Base::next_expiry;

    template<typename...ArgsT>
    explicit BasicMultiReactor(ArgsT...args)
    : impls_(std::forward<ArgsT>(args)...)                                        // data
    {}

    /// return poll ctl function from file descriptor
    IPoller* poller(int fd) override {
        std::size_t ix = fd & (~CustomFDMask);
        std::size_t rix = ix >> CustomFDBits;
        if((fd & HighBitFDMask)==0) {
            rix = ImplsSize-1;
        }
        IPoller* poller=nullptr;
        tuple_for_each(impls_, [&poller, &rix](auto &impl) {
            if(rix==0) {
                poller = &impl;
            }
            rix--;
        });
        return poller;
    }

    template<typename T>
    auto& get() {
        return std::get<boost::mp11::mp_find<ImplsTuple, T>::value>(impls_);
    }

    template<typename T>
    int socket() {
        constexpr std::size_t index = boost::mp11::mp_find<ImplsTuple, T>::value;
        return ((index<<CustomFDBits)|HighBitFDMask) + get<T>().socket();
    }
    /// event loop cycle
    void run() override {
        state(State::PendingOpen);
        state(State::Open);
        std::size_t i {0};
        while (!Base::stop_.load(std::memory_order_acquire)) {
            // Busy-wait for a small number of cycles after work was done.
            if (poll(CyclTime::now(), i++ < 100 ? 0s : NoTimeout) > 0) {
                // Reset counter when work has been done.
                i = 0;
            }
        }
        state(State::PendingClosed);
        state(State::Closed);
    }

    /// busy poll for events, uses inlined implementations
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
    void wakeup() noexcept override { 
        if(state()==State::Open) {
            // wakeup last reactor since others are always busy-polled
            std::get<ImplsSize-1>(impls_).wakeup(); 
        }
    }
protected:
    std::tuple<ImplsT...> impls_;
};
} // namespace io
} // namespace toolbox

namespace toolbox {
    namespace os {
        /// default linux reactor
        using Reactor = BasicMultiReactor<Epoll>;
    }
}

