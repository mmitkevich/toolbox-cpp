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
#include "toolbox/io/PollHandle.hpp"
#include "toolbox/util/Exception.hpp"
#include "toolbox/sys/Log.hpp"


using namespace toolbox::io;

bool Epoll::ctl(PollHandle& handle) {
    auto fd = handle.fd();
    auto ix = static_cast<std::size_t>(fd);
    auto events = handle.events();
    if(epoll_mode_&EpollEt) {
        events = events + PollEvents::Read + PollEvents::Write;
    }
    if(handle.empty()) {
        if(ix<data_.size()) {
            del(fd);
            data_[ix].reset();
        } else {
            return false;
        }
    } else {
        if (ix >= data_.size()) {
            data_.resize(ix + 1);
        }
        auto& ref = data_[ix];
        if(ref.empty()) {
            handle.next_sid();                          // initial subscribe            
            add(fd, handle.sid(), events);     // throws on error
            ref = handle;                               // commit
        } else if(ref.sid()!=handle.sid()) { 
            return false;
        } else {
            if(events!=ref.events()) {
                mod(fd, handle.sid(), events);     // throws on error
            }
            ref.events(events);                // commit
            ref.slot(handle.slot());
        }
    }
    return true;
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
            TOOLBOX_DUMP<<"epoll_ready fd="<<fd<<" events="<<evs;
            s.invoke(now, fd, evs);
        } catch (const std::exception& e) {
            TOOLBOX_ERROR << "error handling io event: " << e.what();
        }
        ++work;
    }
    ready_ = 0;
    return work;
}
