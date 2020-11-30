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

#ifndef TOOLBOX_NET_STREAMCONNECTOR_HPP
#define TOOLBOX_NET_STREAMCONNECTOR_HPP

#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Poller.hpp"
#include "toolbox/sys/Error.hpp"
#include <asm-generic/errno.h>
#include <exception>
#include <system_error>
#include <toolbox/io/Event.hpp>
#include <toolbox/io/Reactor.hpp>
#include <toolbox/net/StreamSock.hpp>

namespace toolbox {
inline namespace net {

enum class SockFn {
    None    = 0,
    Connect = 1,
    Write   = 9,
    Read    = 17,
    Recv    = 18,
};

template<typename EndpointT>
class SockConnOp {
public:    
    using Slot = util::Slot<std::error_code>;
    using Endpoint = EndpointT;
public:
    SockConnOp() = default;
    SockConnOp(SockFn fn, Slot slot, const Endpoint& ep)
    : fn(fn) 
    , slot(slot)
    , endpoint(ep)
    {}
public:
    void complete(std::error_code ec) { 
        slot.invoke(ec);
        *this = {};
    }
    bool empty() const { return slot.empty(); }
public:
    SockFn fn {SockFn::None};    
    Slot slot {};
    Endpoint endpoint {};
};


template<typename BufferT>
class SockBufOp {
public:
    using Buffer = BufferT;
    using Slot = util::Slot<ssize_t, std::error_code>;
public:
    SockBufOp() = default;
    SockBufOp(SockFn fn, Slot slot, Buffer buf, int flags=0)
    : fn(fn)
    , slot(slot)
    , buf(buf)
    , flags(flags) {}
public:
    void complete(ssize_t size, std::error_code ec) { 
        slot.invoke(size, ec);
        *this = {};
    }
    bool empty() const { return slot.empty(); }
public:
    SockFn fn {SockFn::None};
    Slot slot {};
    Buffer buf {};
    int flags {};
};

template<typename SockT>
std::size_t read(SockT& sock, MutableBuffer& buffer, std::error_code& ec) {
    std::size_t total_bytes_read = 0;
    for(std::size_t i=0; buffer.size()>0; i++) {
        std::error_code ec {};
        ssize_t bytes_read = sock.read(buffer, ec);
        if(ec)
            break;
        assert(bytes_read >= 0);
        total_bytes_read += bytes_read;
        buffer += bytes_read;
    }
    return total_bytes_read;
}

/// async tcp socket
template<typename SockT>
class AsyncStreamSock {
    enum class State {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        ConnFailed = 3,
        Failed = 4,
        Disconnecting = 5
    };
    using This = AsyncStreamSock;
    
  public:
    using Protocol = typename SockT::Protocol;
    using Endpoint = typename SockT::Endpoint;

    explicit AsyncStreamSock(Reactor& reactor)
    : reactor_ {reactor}
    , state_ {State::Disconnected}
    {
    }

    ~AsyncStreamSock() {
        close();
    }

    // Copy.
    AsyncStreamSock(const AsyncStreamSock&) = delete;
    AsyncStreamSock& operator=(const AsyncStreamSock&) = delete;

    // Move.
    AsyncStreamSock(AsyncStreamSock&&) noexcept = default;
    AsyncStreamSock& operator=(AsyncStreamSock&&) noexcept = default;

    static constexpr PollEvents poll_events() { return PollEvents::Read + PollEvents::Write; }

    void open(Protocol protocol) {
        sock_ = SockT(protocol);
        sock_.set_non_block();
        if (sock_.is_ip_family()) {
            sock_.set_tcp_no_delay(true);
        }
        sub_ = reactor_.subscribe(sock_.get(), 
            poll_events(), 
            bind<&This::on_conn_event>(this));
    }

    void close() {
        sub_.reset();
        sock_.close();        
    }
    /*
     * Returns true if connection was established synchronously or false if connection is pending
     * asynchronous completion.
     */
    void connect(const Endpoint& ep, Slot<std::error_code> slot) {
        state_ = State::Connecting;
        if(!conn_.empty()) {
            throw std::system_error{make_error_code(std::errc::operation_in_progress), "connect"};
        }
        std::error_code ec {};
        sock_.connect(ep, ec);
        if (ec && ec.value() != EINPROGRESS) { //ec != std::errc::operation_in_progress
            throw std::system_error{ec, "connect"};
        }
        conn_ = {SockFn::Connect, slot, ep};
        if(!ec) {
            conn_.complete(ec);
        }
    }


    void read(MutableBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        assert(state_==State::Connected);
        if(!read_.empty()) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "read" };
        }
        read_ = {SockFn::Read, slot, buffer};
    }
    void recv(MutableBuffer buffer, int flags, Slot<ssize_t, std::error_code> slot) {
        assert(state_==State::Connected);
        if(!read_.empty()) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "recv" };
        }
        read_ = {SockFn::Recv, slot, buffer, flags};
    }
    void write(ConstBuffer buffer, Slot<ssize_t, std::error_code> slot) {
        assert(state_==State::Connected);
        if(!write_.empty()) {
            throw std::system_error { make_error_code(std::errc::operation_in_progress), "write" };
        }   
        write_ = {SockFn::Write, slot, buffer, 0};
    }
  protected:
    void on_conn_event(CyclTime now, os::FD fd, PollEvents events) {
        assert(state_==State::Connecting);
        std::error_code ec {};
        if(events & PollEvents::Error) {
            state_ = State::ConnFailed;
            ec = sock_.get_error();
        } else if(events & PollEvents::Write) {
            state_ = State::Connected;
        }
        sub_.resubscribe(poll_events(), util::bind<&This::on_io_event>(this));
        conn_.complete(ec);
    }
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {
        assert(state_==State::Connected);
        std::error_code ec {};
        ssize_t size {};
        if((events & PollEvents::Write) && !write_.empty()) {
            size = sock_.write(write_.buf, ec);
            write_.complete(size, ec);
        }
        if((events & PollEvents::Read) && !read_.empty()) {
            switch(read_.fn) {
                case SockFn::Recv:
                    size = sock_.recv(read_.buf, read_.flags, ec);
                    break;
                default:
                    size = sock_.read(read_.buf, ec);
                    break;
            }
            read_.complete(size, ec);
        }
    }
    SockT sock_;
    Reactor::Handle sub_;
    Reactor& reactor_;
    SockConnOp<Endpoint> conn_;
    SockBufOp<MutableBuffer> read_;
    SockBufOp<ConstBuffer> write_;
    State state_ {};
};

} // namespace net
} // namespace toolbox

#endif // TOOLBOX_NET_STREAMCONNECTOR_HPP
