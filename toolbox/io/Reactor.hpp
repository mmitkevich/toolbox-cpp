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
#include "toolbox/io/Handle.hpp"
#include <cstdint>
#include <system_error>
#include <toolbox/io/Scheduler.hpp>
#include <toolbox/sys/Error.hpp>
#include <toolbox/io/Epoll.hpp>
#include <toolbox/io/Qpoll.hpp>
#include <toolbox/io/EventFd.hpp>
#include <toolbox/io/Runner.hpp>
#include <toolbox/ipc/MagicRingBuffer.hpp>
#include <utility>
#include <toolbox/util/Tuple.hpp>

namespace toolbox {
inline namespace io {


template<typename...PollersT >
class BasicReactor : public Scheduler {
public:
    using This = BasicReactor<PollersT...>;
    using Base = Scheduler;
    using Handle = PollHandle;
    using Runner = BasicRunner<This>;
    
    static_assert(sizeof...(PollersT)>0, "at least one Poller required");

    static constexpr unsigned FDLimit = 0x80000; // per poller

    static constexpr FD CustomFDMask = (1U<<31);  // high bit means custom fd
public:
    template<typename...ArgsT>
    explicit BasicReactor(ArgsT...args)
    : pollers_(std::forward<ArgsT>(args)...)                                        // data
    {}
/*
    FD ringbuf(const char* path) {
        rbs_.emplace_back(path);
        return rbs_.size() | FD_MASK;
    }

    FD ringbuf(FileHandle&& fd) {
        rbs_.emplace_back(fd);
        return rbs_.size() | FD_MASK;
    }

    ssize_t read(FD fd, void* buf, std::size_t len, std::error_code& ec) noexcept {
        std::size_t index = -1;
        FDType fdt = fdtype(fd, index, ec);
        switch(fdt) {
            case FDType::MagicRingBuf:
                return (ssize_t) rbs_[index].write(buf, len);
            case FDType::Native:
                return os::read(fd, buf, len, ec);
            default:
                return -1;
        } 
    }

    ssize_t write(FD fd, void* buf, std::size_t len, std::error_code& ec) noexcept {
        std::size_t index = -1;
        FDType fdt = fdtype(fd, index, ec);
        switch(fdt) {
            case FDType::MagicRingBuf:
                return (ssize_t) rbs_[index].read(buf, len);
            case FDType::Native:
                return os::read(fd, buf, len, ec);
            default:
                return -1;
        } 
    }*/

    IPoller* poller(FD fd) {
        // high bit set in FD means this is custom poller
        constexpr std::size_t PollersSize = sizeof...(PollersT);
        if(!(fd & CustomFDMask)) {
            return static_cast<IPoller*>(tuple_addressof(pollers_, PollersSize-1));    // last poller assumed as "system"
        }
        std::size_t i = fd & (~CustomFDMask);
        std::size_t poller_index = i / FDLimit;
        std::size_t poller_fd = i % FDLimit;
        if(poller_index>=PollersSize)
            return nullptr;
        return static_cast<IPoller*>(tuple_addressof(pollers_, poller_index));
    }

    // clang-format off
    [[nodiscard]] 
    PollHandle subscribe(FD fd, PollEvents events, IoSlot slot) {
        IPoller* pollr = poller(fd);
        if(!pollr)
            throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
        return pollr->subscribe(fd, events, slot);
    }
  
    int poll(CyclTime now, Duration timeout = NoTimeout);
    
    void run();
protected:
    /// Thread-safe.
    void do_wakeup() noexcept final { std::get<sizeof...(PollersT)-1>(pollers_).do_wakeup(); }

private:
    std::tuple<PollersT...> pollers_;
    std::array<IPoller*, sizeof...(PollersT)> ipollers_;
};


template<typename...PollersT>
inline int BasicReactor<PollersT...>::poll(CyclTime now, Duration timeout)
{
    enum { High = 0, Low = 1 };
    using namespace std::chrono;

    // If timeout is zero then the wait_until time should also be zero to signify no wait.
    MonoTime wait_until{};
    if (!is_zero(timeout) && hooks_.empty()) {
        const MonoTime next = next_expiry(timeout == NoTimeout ? MonoClock::max() : now.mono_time() + timeout);
        if (next > now.mono_time()) {
            wait_until = next;
        }
    }
    int rn = 0;
    std::size_t i = sizeof...(PollersT);
    tuple_for_each(pollers_, [&](auto &pollr) {
        i--;
        int n = 0;
        std::error_code ec;
        if(i>0) {
            n = pollr.wait(MonoTime{}, ec); // no blocking on pollers except last one
        } else if (wait_until < MonoClock::max()) {
            // The wait function will not block if time is zero.
            n = pollr.wait(wait_until, ec);
        } else {
            // Block indefinitely.
            n = pollr.wait(ec);
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
            rn += tqs_[High].dispatch(now) + pollr.dispatch(now);
            // Low priority timers are only dispatched during empty cycles.
            if (i==0 && n == 0) {
                rn += tqs_[Low].dispatch(now);
            }
        }
    });
    io::dispatch(now, hooks_);
    return rn;
}

template<typename... PollersT>
void BasicReactor<PollersT...>::run()
{
    state(State::Starting);
    state(State::Started);
    std::size_t i{0};
    while (!stop_.load(std::memory_order_acquire)) {
        // Busy-wait for a small number of cycles after work was done.
        if (poll(CyclTime::now(), i++ < 100 ? 0s : NoTimeout) > 0) {
            // Reset counter when work has been done.
            i = 0;
        }
    }
    state(State::Stopping);
    state(State::Stopped);
}

using Reactor = BasicReactor<Epoll>;


} // namespace io
} // namespace toolbox

#endif // TOOLBOX_IO_REACTOR_HPP
