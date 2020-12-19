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

#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/PollHandle.hpp"
#include "toolbox/net/DgramSock.hpp"
#include "toolbox/sys/Error.hpp"
#include <exception>
#include <system_error>
#include <toolbox/io/Event.hpp>
#include <toolbox/io/Reactor.hpp>
#include <toolbox/net/McastSock.hpp>
#include <toolbox/io/Socket.hpp>

namespace toolbox {
inline namespace io {


template<typename SockT, typename StateT>
class BasicMcastSocket : public BasicSocket<
  BasicMcastSocket<SockT, StateT>
  , SockT, StateT>
{
    using This = BasicMcastSocket<SockT, StateT>;
    using Base = BasicSocket<This, SockT, StateT>;
  public:
    using typename Base::Sock, typename Base::PollHandle;
    using typename Base::Protocol, typename Base::Endpoint;
  public:
    using Base::Base;
    using Base::poll;
    using Base::open, Base::bind;
    using Base::read, Base::write, Base::recv; 
    using Base::connect, Base::disconnect;
    using Base::leave_group, Base::join_group;

    void async_connect(const Endpoint& ep, util::Slot<std::error_code> slot) {
      std::error_code ec {};
      connect(ep, ec);
      slot(ec);
    }
};

using McastSocket = BasicMcastSocket<McastSock, io::SocketState>;

} // namespace net
} // namespace toolbox
