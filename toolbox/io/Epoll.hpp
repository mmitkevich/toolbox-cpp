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

#include "toolbox/net/Sock.hpp"
#include <sstream>
#include <stdexcept>
#include <toolbox/io/Handle.hpp>
#include <toolbox/io/TimerFd.hpp>
#include <toolbox/sys/Error.hpp>
#include <toolbox/util/Slot.hpp>
#include <toolbox/io/EventFd.hpp>
#include <toolbox/io/PollHandle.hpp>
#include <toolbox/sys/Log.hpp>
#include <sys/epoll.h>

namespace toolbox {
namespace os {

/// Open a new epoll instance. The size argument is ignored in kernels >= 2.6.8, but must be greater
/// than zero.
inline FileHandle epoll_create(int size, std::error_code& ec) noexcept
{
    const auto ret = ::epoll_create(size);
    if (ret < 0) {
        ec = make_sys_error(errno);
    }
    return ret;
}

/// Open a new epoll instance. The size argument is ignored in kernels >= 2.6.8, but must be greater
/// than zero.
inline FileHandle epoll_create(int size)
{
    const auto fd = ::epoll_create(size);
    if (fd < 0) {
        throw std::system_error{make_sys_error(errno), "epoll_create"};
    }
    return fd;
}

/// Open a new epoll instance. If flags is zero, then epoll_create1() is the same as epoll_create().
inline FileHandle epoll_create1(int flags, std::error_code& ec) noexcept
{
    const auto ret = ::epoll_create1(flags);
    if (ret < 0) {
        ec = make_sys_error(errno);
    }
    return ret;
}

/// Open a new epoll instance. If flags is zero, then epoll_create1() is the same as epoll_create().
inline FileHandle epoll_create1(int flags)
{
    const auto fd = ::epoll_create1(flags);
    if (fd < 0) {
        throw std::system_error{make_sys_error(errno), "epoll_create1"};
    }
    return fd;
}

/// Control interface for an epoll file descriptor.
inline int epoll_ctl(int epfd, int op, FD fd, epoll_event event, std::error_code& ec) noexcept
{
    const auto ret = ::epoll_ctl(epfd, op, fd, &event);
    if (ret < 0) {
        ec = make_sys_error(errno);
    }
    return ret;
}

/// Control interface for an epoll file descriptor.
inline void epoll_ctl(int epfd, int op, FD fd, epoll_event event)
{
    const auto ret = ::epoll_ctl(epfd, op, fd, &event);
    if (ret < 0) {
        throw std::system_error{make_sys_error(errno), "epoll_ctl"};
    }
}

/// Wait for an I/O event on an epoll file descriptor.
inline int epoll_wait(int epfd, epoll_event* events, int maxevents, int timeout,
                      std::error_code& ec) noexcept
{
    const auto ret = ::epoll_wait(epfd, events, maxevents, timeout);
    if (ret < 0) {
        ec = make_sys_error(errno);
    }
    return ret;
}

/// Wait for an I/O event on an epoll file descriptor.
inline int epoll_wait(int epfd, epoll_event* events, int maxevents, int timeout)
{
    const auto ret = ::epoll_wait(epfd, events, maxevents, timeout);
    if (ret < 0) {
        throw std::system_error{make_sys_error(errno), "epoll_wait"};
    }
    return ret;
}

} // namespace os
inline namespace io {

enum : unsigned {
    /// The associated file is available for read(2) operations.
    EpollIn = EPOLLIN,

    /// The associated file is available for write(2) operations.
    EpollOut = EPOLLOUT,

    /// Stream socket peer closed connection, or shut down writing half of connection. (This flag is
    /// especially useful for writing simple code to detect peer shutdown when using Edge-Triggered
    /// monitoring.)
    EpollRdHup = EPOLLRDHUP,

    /// There is an exceptional condition on the file descriptor. See the discussion of POLLPRI in
    /// poll(2).
    EpollPri = EPOLLPRI,

    /// Error condition happened on the associated file descriptor. This event is also reported for
    /// the write end of a pipe when the read end has been closed. epoll_wait(2) will always report
    /// for this event; it is not necessary to set it in events.
    EpollErr = EPOLLERR,

    /// Hang up happened on the associated file descriptor. epoll_wait(2) will always wait for this
    /// event; it is not necessary to set it in events.
    ///
    /// Note that when reading from a channel such as a pipe or a stream socket, this event merely
    /// indicates that the peer closed its end of the channel. Subsequent reads from the channel
    /// will return 0 (end of file) only after all outstanding data in the channel has been
    /// consumed.
    EpollHup = EPOLLHUP,

    /// Sets the Edge-Triggered behavior for the associated file descriptor. The default behavior
    /// for epoll is Level-Triggered. See epoll(7) for more detailed information about Edge and
    /// Level-Triggered event distribution architectures.
    EpollEt = EPOLLET,

    /// Sets the one-shot behavior for the associated file descriptor. This means that after an
    /// event is pulled out with epoll_wait(2) the associated file descriptor is internally disabled
    /// and no other events will be reported by the epoll interface. The user must call epoll_ctl()
    /// with EPOLL_CTL_MOD to rearm the file descriptor with a new event mask.
    EpollOneShot = EPOLLONESHOT,
};

using EpollEvent = epoll_event;

/// This is Epoll reactor implementation
class TOOLBOX_API Epoll {
  public:
    using Event = EpollEvent;
    using This = Epoll;
    static constexpr std::size_t MaxEvents{128};
    using FD = typename FileHandle::FD;
    
    using Handle = PollHandle;

    static constexpr FD fd(const Event& ev) noexcept
    {
        return static_cast<FD>(ev.data.u64 & 0xffffffff);
    }
    static constexpr int sid(const Event& ev) noexcept
    {
        return static_cast<int>(ev.data.u64 >> 32);
    }
    
    explicit Epoll(std::size_t size_hint = 0, int flags = 0)
    : epfd_{os::epoll_create1(flags)}
    , tfd_{TFD_NONBLOCK}
    {
        add(tfd_.fd(), 0, PollEvents::Read);
        const auto notify = notify_.fd();
        add(notify, 0, PollEvents::Read);
        data_.resize(std::max<size_t>(notify + 1, size_hint));
    }

    ~Epoll() {
        del(tfd_.fd()); 
        del(notify_.fd());
    }

    // Copy.
    Epoll(const Epoll&) = delete;
    Epoll& operator=(const Epoll&) = delete;

    // Move.
    Epoll(Epoll&&) = default;
    Epoll& operator=(Epoll&&) = default;

    /// Returns the timer file descriptor.
    /// Exposing the timer file descriptor allows callers to check if one of the signalled events
    /// was trigger by the timer.
    int timer_fd() const noexcept { return tfd_.fd(); }

    void swap(Epoll& rhs) noexcept { 
        std::swap(epfd_, rhs.epfd_);
        std::swap(tfd_, rhs.tfd_);
        std::swap(events_, rhs.events_);
        std::swap(data_, rhs.data_);
    }

    /// blocks forever
    /// Returns the number of file descriptors that are ready.
    int wait(std::error_code& ec) noexcept
    {
        MonoTime timeout{};
        // Only set the timer if it has changed.
        if (timeout != timeout_) {
            // A zero timeout will disarm the timer.
            tfd_.set_time(0, timeout, ec);
            if (ec) {
                return 0;
            }
            timeout_ = timeout;
        }
        //TOOLBOX_DUMP<<"epoll_wait timeout=-1";
        ready_ =  os::epoll_wait(*epfd_, events_.data(), (int)events_.size(), -1, ec);
        return ready_;
    }
    /// Returns the number of file descriptors that are ready, or zero if no file descriptor became
    /// ready during before the operation timed-out.
    /// The number of file descriptors returnes may include the timer file descriptor,
    /// so callers must check for the presence of this descriptor.
    int wait(MonoTime timeout, std::error_code& ec) noexcept
    {
        // Only set the timer if it has changed.
        if (timeout != timeout_) {
            // A zero timeout will disarm the timer.
            tfd_.set_time(0, timeout, ec);
            if (ec) {
                return 0;
            }
            timeout_ = timeout;
        }
        //TOOLBOX_DUMP<<"epoll_wait timeout="<<timeout;
        // Do not block if timer is zero.
        ready_ =  os::epoll_wait(*epfd_, events_.data(), (int)events_.size(), is_zero(timeout) ? 0 : -1, ec);
        return ready_;
    }

    /// modifies subscription
    bool ctl(PollHandle& handle);

    int socket() {
        throw std::runtime_error("not implemented");
    }

    int dispatch(CyclTime now)
    {
        int work{0};
        for (std::size_t i = 0; i < ready_; ++i) {

            auto& ev = events_[i];
            const FD fd = Epoll::fd(ev);
            if (fd == notify_.fd()) {
                notify_.read();
                continue;
            }
            const PollFD& ref = data_[fd];
            auto s = ref.slot();
            if (!s) {
                // Ignore timerfd.
                continue;
            }

            const int sid = Epoll::sid(ev);
            // Skip this socket if it was modified after the call to wait().
            if (ref.sid() > sid) {
                continue;
            }
            // Apply the interest events to filter-out any events that the user may have removed from
            // the events since the call to wait() was made. This would typically happen via a reentrant
            // call into the reactor from an event-handler. N.B. EpollErr and EpollHup are always
            // reported if they occur, regardless of whether they are specified in events.
            const uint32_t events = ev.events & (to_epoll_events(ref.events()) | EpollErr | EpollHup);
            if (!events) {
                continue;
            }

            try {
                assert(s!=nullptr);
                auto evs = from_epoll_events(events);
                //TOOLBOX_DUMP<<"epoll_ready fd="<<fd<<" events="<<evs;
                s(now, fd, evs);
            } catch (const std::exception& e) {
                TOOLBOX_ERROR << "error handling io event: " << e.what();
            }
            ++work;
        }
        ready_ = 0;
        return work;
    }
    
    void wakeup() noexcept {
        // Best effort.
        std::error_code ec;
        notify_.write(1, ec);
    }
    bool constexpr is_et_mode() const { return epoll_mode_ == EpollEt; }
private:
    void add(FD fd, int sid, PollEvents events)
    {
        Event ev;
        mod(ev, fd, sid, events);
        TOOLBOX_DUMP<<"epoll_ctl_add fd="<<fd<<" ev="<<std::hex<<(unsigned)ev.events<<std::dec;
        os::epoll_ctl(*epfd_, EPOLL_CTL_ADD, fd, ev);
    }
    void del(FD fd)
    {
        // In kernel versions before 2.6.9, the EPOLL_CTL_DEL operation required a non-null pointer
        // in event, even though this argument is ignored.
        Event ev{};
        TOOLBOX_DUMP<<"epoll_ctl_del fd="<<fd<<" ev="<<std::hex<<(unsigned)ev.events<<std::dec;
        os::epoll_ctl(*epfd_, EPOLL_CTL_DEL, fd, ev);
    }
    /// throws system_error
    void mod(FD fd, int sid, PollEvents events)
    {
        Event ev;
        mod(ev, fd, sid, events);
        TOOLBOX_DUMP<<"epoll_ctl_mod fd="<<fd<<" ev="<<std::hex<<(unsigned)ev.events<<std::dec;
        os::epoll_ctl(*epfd_, EPOLL_CTL_MOD, fd, ev);
    }
    uint32_t to_epoll_events(PollEvents events) {
        uint32_t result = epoll_mode_;
        if(events & PollEvents::Read)
            result |= EpollIn;
        if(events & PollEvents::Write)
            result |= EpollOut;
        if(events & PollEvents::Error)
            result |= EpollErr|EpollHup;
        if(events & PollEvents::ET)
            result |= EpollEt;
        return result;
    }
    PollEvents from_epoll_events(uint32_t epoll_mask) {
        PollEvents result = PollEvents::None;
        if(epoll_mask & EpollIn)
            result = (PollEvents)(result | PollEvents::Read);
        if(epoll_mask & EpollOut)
            result = (PollEvents)(result | PollEvents::Write);
        if(epoll_mask & (EpollErr|EpollHup))
            result = (PollEvents)(result|PollEvents::Error);
        return result;
    }
    void mod(Event& ev, FD fd, int sid, PollEvents events) noexcept
    {
        auto epoll_events = to_epoll_events(events); 
        auto u64 = static_cast<std::uint64_t>(sid) << 32 | fd;
        ev.events = epoll_events;
        ev.data.u64 = u64;
    }
    FileHandle epfd_;
    TimerFd<MonoClock> tfd_;
    MonoTime timeout_{};
    std::vector<PollFD> data_;
    EventFd notify_{0, EFD_NONBLOCK};
    std::array<Event,MaxEvents> events_;
    std::size_t ready_{};
    static constexpr unsigned  epoll_mode_ {0};
    //static constexpr unsigned epoll_mode_ {EpollEt};
};

} // namespace io
} // namespace toolbox
