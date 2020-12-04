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


template<typename SockT, typename PollT, typename DerivedT>
class BasicMcastSocket : public BasicSocket<SockT, PollT, DerivedT> {
    using This = BasicMcastSocket<SockT, PollT,  DerivedT>;
    using Base = BasicSocket<SockT, PollT, DerivedT>;
  public:
    using typename Base::Sock, typename Base::PollHandle;
    using typename Base::Protocol, typename Base::Endpoint;
  public:
    using Base::Base;
    using Base::sock, Base::poll;
    using Base::open, Base::bind;
    using Base::read, Base::write, Base::recv; 

    template<typename AddrT, typename IfaceT>
    void join_group(const AddrT& addr, IfaceT iface) {
      return sock().join_group(addr, std::forward<IfaceT>(iface));
    }
    template<typename AddrT, typename IfaceT>
    void leave_group(const AddrT& addr, IfaceT iface) {
      return sock().leave_group(addr, std::forward<IfaceT>(iface));
    }
};

class McastSocket: public BasicMcastSocket<McastSock, PollHandle, McastSocket>
{
    using Base = BasicMcastSocket<McastSock, PollHandle, McastSocket>;
public:
    using Sock = typename Base::Sock;
    using Protocol = typename Sock::Protocol;
    using Endpoint = typename Sock::Endpoint;
public:
    using Base::Base;
    using Base::sock, Base::poll;
    using Base::open, Base::bind;
    using Base::read, Base::write, Base::recv; 
    using Base::join_group, Base::leave_group;
};

} // namespace net
} // namespace toolbox
