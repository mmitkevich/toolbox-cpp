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
#include <cstdint>
#include <utility>

#include <toolbox/io/Scheduler.hpp>
#include <toolbox/sys/Error.hpp>

#include "toolbox/io/Handle.hpp"

#include <toolbox/io/ReactorHandle.hpp>

#include <toolbox/io/Epoll.hpp>
#include <toolbox/io/Qpoll.hpp>

#include <toolbox/io/EventFd.hpp>

#include <toolbox/io/Runner.hpp>

#include <toolbox/util/Tuple.hpp>

namespace toolbox {
inline namespace io {

template<typename SchedulerT, typename DerivedT>
class BasicReactor : public SchedulerT {
    using Scheduler = SchedulerT;
public:
    using Scheduler::state;
    // clang-format on
    void add_hook(Hook& hook) noexcept { hooks_.push_back(hook); }
    HookList& hooks() noexcept { return hooks_; }
    void run()
    {
        state(State::Starting);
        state(State::Started);
        std::size_t i {0};
        while (!Scheduler::stop_.load(std::memory_order_acquire)) {
            // Busy-wait for a small number of cycles after work was done.
            if (static_cast<DerivedT*>(this)->poll(CyclTime::now(), i++ < 100 ? 0s : NoTimeout) > 0) {
                // Reset counter when work has been done.
                i = 0;
            }
        }
        state(State::Stopping);
        state(State::Stopped);
    }
protected:
    HookList hooks_;
};

/// multi-reactor
template<typename SchedulerT, typename...ReactorsT>
class BasicMultiReactor: public BasicReactor<SchedulerT, BasicMultiReactor<SchedulerT, ReactorsT...> > {
public:
    using This = BasicMultiReactor<SchedulerT, ReactorsT...>;
    using Base = BasicReactor<SchedulerT, This>;
    using Scheduler = SchedulerT;
    using Handle = PollHandle;
    using Runner = BasicRunner<This>;
    using FD = os::FD;

    static_assert(sizeof...(ReactorsT)>0, "at least one Reactor implementation required");

    static constexpr unsigned CustomFDBits = 12; // per reactor
    static constexpr unsigned CustomFDMask = (1U<<12)-1; // per reactor

    static constexpr FD HighBitFDMask = (1U<<31);  // high bit means custom fd
public:
    template<typename...ArgsT>
    explicit BasicMultiReactor(ArgsT...args)
    : reactors_(std::forward<ArgsT>(args)...)                                        // data
    {}
    
    using Base::hooks;
    using Base::next_expiry;
    using Base::timers;

    constexpr static std::size_t ReactorsSize = sizeof...(ReactorsT);

    IReactor* reactor(FD fd) {
        // high bit set in FD means this is custom poller

        if(!(fd & HighBitFDMask)) {
            return static_cast<IReactor*>(tuple_addressof(reactors_, ReactorsSize-1));    // last poller assumed as "system"
        }
        std::size_t ix = fd & (~CustomFDMask);
        std::size_t rix = ix >> CustomFDBits;
        std::size_t rfd = ix & CustomFDMask;
        if(rix>=ReactorsSize)
            return nullptr;
        return static_cast<IReactor*>(tuple_addressof(reactors_, rix));
    }

    // clang-format off
    [[nodiscard]] 
    PollHandle subscribe(FD fd, PollEvents events, IoSlot slot) {
        IReactor* r = reactor(fd);
        if(!r)
            throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
        PollHandle handle(r, fd, 0);
        subscribe(handle, events, slot);
        return handle;
    }

    void subscribe(PollHandle& handle, PollEvents events, IoSlot slot) {
        IReactor* r = handle.reactor();
        assert(r!=nullptr);
        r->subscribe(handle, events, slot);
    }

    int poll(CyclTime now, Duration timeout = NoTimeout);
    void wakeup() noexcept { 
        // wakeup last reactor since others are always busy-polled
        std::get<ReactorsSize-1>(reactors_).wakeup(); 
    }
protected:
    std::tuple<ReactorImpl<ReactorsT>...> reactors_;
};


template<typename SchedulerT, typename...ReactorsT>
inline int BasicMultiReactor<SchedulerT, ReactorsT...>::poll(CyclTime now, Duration timeout)
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
    std::size_t i = sizeof...(ReactorsT);
    tuple_for_each(reactors_, [&](auto &r) {
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

using Reactor = BasicMultiReactor<Scheduler, Epoll>;


} // namespace io
} // namespace toolbox

#endif // TOOLBOX_IO_REACTOR_HPP
