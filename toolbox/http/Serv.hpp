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

#ifndef TOOLBOX_HTTP_SERV_HPP
#define TOOLBOX_HTTP_SERV_HPP

//#include <toolbox/http/App.hpp>
#include <toolbox/http/Conn.hpp>
#include <toolbox/net/StreamAcceptor.hpp>

namespace toolbox {
inline namespace http {

template <typename ConnT = HttpConn>
class BasicHttpServ : public StreamAcceptor<BasicHttpServ<ConnT>> {
    using This = BasicHttpServ<ConnT>;
    using Base =  StreamAcceptor<This>;
    friend Base;
    using ConstantTimeSizeOption = boost::intrusive::constant_time_size<false>;
    using MemberHookOption
        = boost::intrusive::member_hook<ConnT, decltype(ConnT::list_hook), &ConnT::list_hook>;
    using ConnList = boost::intrusive::list<ConnT, ConstantTimeSizeOption, MemberHookOption>;

    using typename Base::Endpoint;
  public:
    using Conn = ConnT;
  public:
    BasicHttpServ(CyclTime now, Reactor& r, const Endpoint& ep)
    : Base {r, ep}
    , reactor_{r}
    {}
    ~BasicHttpServ()
    {
        const auto now = CyclTime::current();
        conn_list_.clear_and_dispose([now](auto* conn) { conn->dispose(now); });
    }

    // Copy.
    BasicHttpServ(const BasicHttpServ&) = delete;
    BasicHttpServ& operator=(const BasicHttpServ&) = delete;

    // Move.
    BasicHttpServ(BasicHttpServ&&) = delete;
    BasicHttpServ& operator=(BasicHttpServ&&) = delete;

    void accept(Slot<CyclTime, Conn&> slot) {
      accept_ = slot;
    }
  private:
    void on_sock_prepare(CyclTime now, IoSock& sock) {}
    void on_sock_accept(CyclTime now, IoSock&& sock, const Endpoint& ep)
    {
        Conn* conn = new Conn{now, reactor_, std::move(sock), ep};
        conn_list_.push_back(*conn);
        if(accept_)
          accept_(now, *conn);
    }

    Reactor& reactor_;
    // List of active connections.
    ConnList conn_list_;
    Slot<CyclTime, Conn&> accept_;
};

using HttpServ = BasicHttpServ<HttpConn>;

} // namespace http
} // namespace toolbox

#endif // TOOLBOX_HTTP_SERV_HPP
