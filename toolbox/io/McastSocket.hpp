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
#include "toolbox/io/Reactor.hpp"
#include "toolbox/net/DgramSock.hpp"
#include "toolbox/sys/Error.hpp"
#include <exception>
#include <system_error>
#include <toolbox/io/Event.hpp>
#include <toolbox/io/MultiReactor.hpp>
#include <toolbox/io/DgramSocket.hpp>
#include <toolbox/net/McastSock.hpp>
#include <toolbox/io/Socket.hpp>

namespace toolbox {
inline namespace io {


template<typename DerivedT, typename SockT>
class BasicMcastSocket : public BasicDgramSocket<DerivedT, SockT>
{
    using This = BasicMcastSocket<DerivedT, SockT>;
    using Base = BasicDgramSocket<DerivedT, SockT>;
  public:
    using typename Base::Sock, typename Base::PollHandle;
    using typename Base::Protocol, typename Base::Endpoint;
  public:
    using Base::Base;
    using Base::poll;
    using Base::open, Base::bind;
    using Base::read, Base::write, Base::recv; 
    using Base::join_group, Base::leave_group;
};

template<class McastSockT=McastSock>
class McastSocket: public BasicMcastSocket<McastSocket<McastSockT>, McastSockT> {
    using Base = BasicMcastSocket<McastSocket<McastSockT>, McastSockT>;
    using typename Base::SocketOpen, typename Base::SocketRead, typename Base::SocketWrite;
  public:
    using Base::Base;

    SocketOpen& open_impl() { return open_impl_; }
    SocketRead& read_impl() { return read_impl_; }
    SocketWrite& write_impl() { return write_impl_; }
  protected:
    SocketOpen open_impl_;
    SocketRead read_impl_;
    SocketWrite write_impl_;
};

} // namespace net
} // namespace toolbox
