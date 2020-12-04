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

#ifndef TOOLBOX_HTTP_CONN_HPP
#define TOOLBOX_HTTP_CONN_HPP

#include "toolbox/io/Handle.hpp"
#include "toolbox/io/PollHandle.hpp"
#include <exception>
#include <toolbox/http/Parser.hpp>
#include <toolbox/http/Request.hpp>
#include <toolbox/http/Stream.hpp>
#include <toolbox/io/Disposer.hpp>
#include <toolbox/io/Event.hpp>
#include <toolbox/io/Reactor.hpp>
#include <toolbox/net/Endpoint.hpp>
#include <toolbox/net/IoSock.hpp>
#include <toolbox/util/MemAlloc.hpp>
#include <toolbox/util/Slot.hpp>
#include <boost/intrusive/list.hpp>

namespace toolbox {
inline namespace http {
class HttpAppBase;

template <typename RequestT, typename ResponseT, bool IsClient>
class BasicHttpConn
: public MemAlloc
, public BasicDisposer<BasicHttpConn<RequestT, ResponseT, IsClient>>
, BasicHttpParser<BasicHttpConn<RequestT, ResponseT, IsClient>> {

    using This = BasicHttpConn<RequestT, ResponseT, IsClient>;
    friend class BasicDisposer<This>;
    friend class BasicHttpParser<This>;
    using Request = RequestT;
    using Response = ResponseT;

    using Parser = BasicHttpParser<This>;
    // Automatically unlink when object is destroyed.
    using AutoUnlinkOption = boost::intrusive::link_mode<boost::intrusive::auto_unlink>;

    static constexpr auto IdleTimeout = 5s;

    using Parser::method;
    using Parser::parse;
    using Parser::should_keep_alive;

  public:
    using Protocol = StreamProtocol;
    using Endpoint = StreamEndpoint;

    BasicHttpConn(CyclTime now, Reactor& r, IoSock&& sock, const Endpoint& ep)
    : Parser{HttpType::Request}
    , reactor_(r)
    , sock_{std::move(sock)}
    , ep_{ep}
    , sub_(sock_.get(), r.ctl(sock_.get()))
    {
        sub_.add(PollEvents::Read, bind<&BasicHttpConn::on_io_event>(this));
        schedule_timeout(now);
        on_http_connect(now, ep_);
    }

    // Copy.
    BasicHttpConn(const BasicHttpConn&) = delete;
    BasicHttpConn& operator=(const BasicHttpConn&) = delete;

    // Move.
    BasicHttpConn(BasicHttpConn&&) = delete;
    BasicHttpConn& operator=(BasicHttpConn&&) = delete;

    const Endpoint& endpoint() const noexcept { return ep_; }
    void clear() noexcept { req_.clear(); }
    boost::intrusive::list_member_hook<AutoUnlinkOption> list_hook;

    void http_connect(Slot<CyclTime, const Endpoint&> slot) {
        http_connect_ = slot;
    }
    void http_disconnect(Slot<CyclTime, const Endpoint&> slot) {
        http_disconnect_ = slot;
    }
    void http_error(Slot<CyclTime, const Endpoint&, std::exception&, HttpStream&> slot) {
        http_error_ = slot;
    }
    void http_timeout(Slot<CyclTime, const Endpoint&> slot) {
        http_timeout_ = slot;
    }
    void http_message(Slot<CyclTime, const Endpoint&, const HttpRequest&, HttpStream&> slot) {
        http_message_ = slot;
    }
    void http_response(Slot<CyclTime, const HttpResponse&, HttpStream&> slot) {
        http_response_ = slot;
    }

    void http_request(HttpRequest&& req) {
        req_ = std::move(req);
        ins_.http_request(req.method(), req.url(), req.headers());
    }
protected:
    void on_http_connect(CyclTime now, const Endpoint& ep) {
        TOOLBOX_INFO << "http_connect, ep:"<<ep;
        if(http_connect_)
            http_connect_(now, ep);
    }
    
    void on_http_disconnect(CyclTime now, const Endpoint& ep) {
        TOOLBOX_INFO << "http_disconnect, ep:"<<ep;
        if(http_disconnect_)
            http_disconnect_(now, ep);        
    }
    void on_http_error(CyclTime now, const Endpoint& ep, const std::exception& e, HttpStream& os) {
        TOOLBOX_INFO << "http_error, ep:"<<ep<<", e:"<<e.what();
        if(http_error_)
            http_error_(now, ep, e, os);        
    }
    void on_http_timeout(CyclTime now, const Endpoint& ep) {
        TOOLBOX_INFO << "http_timeout, ep:"<<ep;
        if(http_timeout_)
            http_timeout_(now, ep);
    }
    void on_http_message(CyclTime now, const Endpoint& ep, const HttpRequest& req, HttpStream& os) {
        TOOLBOX_INFO << "http_message, ep:"<<ep<<", req:"<<req;
        if(http_message_)
            http_message_(now, ep, req, os);
    }
  protected:
    void dispose_now(CyclTime now) noexcept
    {
        on_http_disconnect(now, ep_); // noexcept
        // Best effort to drain any data still pending in the write buffer before the socket is
        // closed.
        if (!out_.empty()) {
            std::error_code ec;
            os::write(sock_.get(), out_.buffer(), ec); // noexcept
        }
        delete this;
    }

  private:
    ~BasicHttpConn() = default;
    bool on_message_begin(CyclTime now) noexcept
    {
        in_progress_ = true;
        req_.clear();
        return true;
    }
    bool on_url(CyclTime now, std::string_view sv) noexcept
    {
        bool ret{false};
        try {
            req_.append_url(sv);
            ret = true;
        } catch (const std::exception& e) {
            on_http_error(now, ep_, e, outs_);
            this->dispose(now);
        }
        return ret;
    }
    bool on_status(CyclTime now, std::string_view sv) noexcept
    {
         // Only supported for HTTP responses.
        resp_.reset();
        resp_.status(sv);
        http_response_(now, resp_, ins_);
        return false;
    }
    bool on_header_field(CyclTime now, std::string_view sv, First first) noexcept
    {
        bool ret{false};
        try {
            if constexpr(IsClient) {
                resp_.append_header_field(sv, first);
            } else {
                req_.append_header_field(sv, first);
            }
            ret = true;
        } catch (const std::exception& e) {
            on_http_error(now, ep_, e, outs_);
            this->dispose(now);
        }
        return ret;
    }
    bool on_header_value(CyclTime now, std::string_view sv, First first) noexcept
    {
        bool ret{false};
        try {
            if constexpr(IsClient) {
                req_.append_header_value(sv, first);
            } else {
                req_.append_header_value(sv, first);
            }
            ret = true;
        } catch (const std::exception& e) {
            on_http_error(now, ep_, e, outs_);
            this->dispose(now);
        }
        return ret;
    }
    bool on_headers_end(CyclTime now) noexcept
    {
        if constexpr(IsClient) {
            
        } else {
            req_.set_method(method());
        }
        return true;
    }
    bool on_body(CyclTime now, std::string_view sv) noexcept
    {
        bool ret{false};
        try {
            req_.append_body(sv);
            ret = true;
        } catch (const std::exception& e) {
            on_http_error(now, ep_, e, outs_);
            this->dispose(now);
        }
        return ret;
    }
    bool on_message_end(CyclTime now) noexcept
    {
        bool ret{false};
        try {
            in_progress_ = false;
            req_.flush(); // May throw.
            on_http_message(now, ep_, req_, outs_);
            ret = true;
        } catch (const std::exception& e) {
            on_http_error(now, ep_, e, outs_);
            this->dispose(now);
        }
        return ret;
    }
    bool on_chunk_header(CyclTime now, std::size_t len) noexcept { return true; }
    bool on_chunk_end(CyclTime now) noexcept { return true; }
    void on_timeout_timer(CyclTime now, Timer& tmr)
    {
        auto lock = this->lock_this(now);
        on_http_timeout(now, ep_);
        this->dispose(now);
    }
    void on_io_event(CyclTime now, os::FD fd, PollEvents events)
    {
        assert(fd==sock_.get());
        auto lock = this->lock_this(now);
        try {
            if (events & PollEvents::Read) {
                if (!drain_input(now, sock_)) {
                    this->dispose(now);
                    return;
                }
            }
            // Do not attempt to flush the output buffer if it is empty or if we are still waiting
            // for the socket to become writable.
            if (out_.empty() || (write_blocked_ && !(events & PollEvents::Write))) {
                return;
            }
            flush_output(now);
        } catch (const HttpException&) {
            // Do not call on_http_error() here, because it will have already been called in one of
            // the noexcept parser callback functions.
        } catch (const std::exception& e) {
            on_http_error(now, ep_, e, outs_);
            this->dispose(now);
        }
    }
    bool drain_input(CyclTime now, IoSock& sock)
    {
        // Limit the number of reads to avoid starvation.
        for (int i{0}; i < 4; ++i) {
            std::error_code ec;
            const auto buf = in_.prepare(2944);
            const auto size = sock.read(buf, ec);
            if (ec) {
                // No data available in socket buffer.
                if (ec == std::errc::operation_would_block) {
                    break;
                }
                throw std::system_error{ec, "read"};
            }
            if (size == 0) {
                // N.B. the socket may still be writable if the peer has performed a shutdown on the
                // write side of the socket only.
                flush_input(now);
                return false;
            }
            // Commit actual bytes read.
            in_.commit(size);
            // Assume that the TCP stream has been drained if we read less than the requested
            // amount.
            if (static_cast<size_t>(size) < buffer_size(buf)) {
                break;
            }
        }
        flush_input(now);
        // Reset timer.
        schedule_timeout(now);
        return true;
    }
    void flush_input(CyclTime now) { 
        in_.consume(parse(now, in_.buffer())); 
    }
    void flush_output(CyclTime now)
    {
        // Attempt to flush buffered data.
        out_.consume(os::write(sock_.get(), out_.buffer()));
        if (out_.empty()) {
            if (!in_progress_ && !should_keep_alive()) {
                this->dispose(now);
                return;
            }
            if (write_blocked_) {
                // Restore read-only state after the buffer has been drained.
                sub_.events(PollEvents::Read);
                write_blocked_ = false;
            }
        } else if (!write_blocked_) {
            // Set the state to read-write if the entire buffer could not be written.
            sub_.events(PollEvents::Read + PollEvents::Write);
            write_blocked_ = true;
        }
    }
    void schedule_timeout(CyclTime now)
    {
        const auto timeout = std::chrono::ceil<Seconds>(now.mono_time() + IdleTimeout);
        tmr_ = reactor_.timer(timeout, Priority::Low, bind<&BasicHttpConn::on_timeout_timer>(this));
    }

    Reactor& reactor_;
    IoSock sock_;
    Endpoint ep_;
    Reactor::Handle sub_;
    Timer tmr_;
    Buffer in_, out_;
    Request req_;
    Response resp_;
    HttpStream ins_{in_};
    HttpStream outs_{out_};
    bool in_progress_{false}, write_blocked_{false};

    Slot<CyclTime, const Endpoint&> http_connect_;
    Slot<CyclTime, const Endpoint&> http_disconnect_;
    Slot<CyclTime, const Endpoint&, const std::exception&, HttpStream&> http_error_;
    Slot<CyclTime, const Endpoint&> http_timeout_;
    Slot<CyclTime, const Endpoint&, const HttpRequest&, HttpStream&> http_message_;
    Slot<CyclTime, const HttpResponse&, HttpStream&> http_response_;
};

using HttpServerConn = BasicHttpConn<HttpRequest, HttpResponse, false>;
using HttpConn = BasicHttpConn<HttpRequest, HttpResponse, true>;

} // namespace http
} // namespace toolbox

#endif // TOOLBOX_HTTP_CONN_HPP
