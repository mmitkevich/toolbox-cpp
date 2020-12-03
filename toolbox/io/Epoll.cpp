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

#include "Epoll.hpp"
#include "toolbox/io/Handle.hpp"
#include "toolbox/io/ReactorHandle.hpp"
#include "toolbox/util/Exception.hpp"
#include "toolbox/sys/Log.hpp"


using namespace toolbox::io;

void Epoll::subscribe(PollHandle& handle, PollEvents events, IoSlot slot)
{
    auto fd = handle.fd();
    assert(slot);
    if (fd >= static_cast<int>(data_.size())) {
        data_.resize(fd + 1);
    }
    auto& ref = data_[fd];
    add(fd, ++ref.sid, events);
    ref.events = events;
    ref.slot = slot;
    handle.sid(ref.sid);
}

void Epoll::unsubscribe(PollHandle& handle) {
    auto& ref = data_[handle.fd()];
    if (ref.sid == handle.sid()) {
        del(handle.fd());
        ref.events = PollEvents::None;
        ref.slot.reset();
    }else {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument));
    }
}


void Epoll::resubscribe(PollHandle& handle, PollEvents events)
{
    auto& ref = data_[handle.fd()];
    if (ref.sid == handle.sid()) {
        if (ref.events != events) {
            mod(handle.fd(), handle.sid(), events);
            ref.events = events;
        }
    }
}

void Epoll::resubscribe(PollHandle& handle, PollEvents events, IoSlot slot)
{
    auto& ref = data_[handle.fd()];
    if (ref.sid == handle.sid()) {
        ref.slot = slot;
        if (ref.events != events) {
            mod(handle.fd(), handle.sid(), events);
            ref.events = events;
        }
    }
}


int Epoll::dispatch(CyclTime now)
{
    int work{0};
    for (std::size_t i = 0; i < ready_; ++i) {

        auto& ev = events_[i];
        const FD fd = Epoll::fd(ev);
        if (fd == notify_.fd()) {
            notify_.read();
            continue;
        }
        const Data& ref = data_[fd];
        if (!ref.slot) {
            // Ignore timerfd.
            continue;
        }

        const int sid = Epoll::sid(ev);
        // Skip this socket if it was modified after the call to wait().
        if (ref.sid > sid) {
            continue;
        }
        // Apply the interest events to filter-out any events that the user may have removed from
        // the events since the call to wait() was made. This would typically happen via a reentrant
        // call into the reactor from an event-handler. N.B. EpollErr and EpollHup are always
        // reported if they occur, regardless of whether they are specified in events.
        const uint32_t events = ev.events & (to_epoll_events(ref.events) | EpollErr | EpollHup);
        if (!events) {
            continue;
        }

        try {
            ref.slot(now, fd, from_epoll_events(events));
        } catch (const std::exception& e) {
            TOOLBOX_ERROR << "error handling io event: " << e.what();
        }
        ++work;
    }
    ready_ = 0;
    return work;
}
