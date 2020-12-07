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
    if constexpr (epoll_mode_==EpollEt) {
        events = events + PollEvents::Read + PollEvents::Write + PollEvents::ET;
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
            add(fd, handle.next_sid(), events);     // initial subscribe            
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


