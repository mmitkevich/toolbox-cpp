#pragma once

#include <toolbox/http/Conn.hpp>
#include <toolbox/net/StreamConnector.hpp>
#include <toolbox/net/Resolver.hpp>

namespace toolbox {
inline namespace http {

class HttpClnt : public StreamConnector<HttpClnt> {
    using Base = StreamConnector<HttpClnt>;
    friend Base;
    using Conn = HttpConn;
    using ConstantTimeSizeOption = boost::intrusive::constant_time_size<false>;
    using MemberHookOption = boost::intrusive::member_hook<Conn, decltype(Conn::list_hook),
                                                           &Conn::list_hook>;
    using ConnList = boost::intrusive::list<Conn, ConstantTimeSizeOption, MemberHookOption>;
  public:
    HttpClnt(CyclTime now, Reactor& reactor, Resolver& resolver, const std::string& uri)
    : reactor_{reactor}
    , resolver_{resolver}
    , uri_{uri}
    , ep_{parse_stream_endpoint(uri)}
    {
        // Immediate and then at 5s intervals.
        tmr_ = reactor_.timer(now.mono_time(), 5s, Priority::Low, bind<&HttpClnt::on_timer>(this));
    }
    ~HttpClnt()
    {
        const auto now = CyclTime::current();
        conn_list_.clear_and_dispose([now](auto* conn) { 
            conn->dispose(now);
        });
        
    }

  private:
    void on_sock_prepare(CyclTime now, IoSock& sock)
    {
        if (sock.is_ip_family()) {
            // Set the number of SYN retransmits that TCP should send before aborting the attempt to
            // connect.
            set_tcp_syn_nt(sock.get(), 1);
        }
    }
    void on_sock_connect(CyclTime now, IoSock&& sock, const Endpoint& ep)
    {
        TOOLBOX_INFO << "connection opened: " << ep;
        inprogress_ = false;

        // High performance TCP servers could use a custom allocator.
        auto* const conn = new Conn{now, reactor_, std::move(sock), ep};
        conn_list_.push_back(*conn);
    }
    void on_sock_connect_error(CyclTime now, const std::exception& e)
    {
        TOOLBOX_ERROR << "failed to connect: " << e.what();
        aifuture_ = resolver_.resolve(uri_, SOCK_STREAM);
        inprogress_ = false;
    }
    void on_timer(CyclTime now, Timer& tmr)
    {
        if (!conn_list_.empty() || inprogress_) {
            return;
        }
        if (aifuture_.valid()) {
            if (!is_ready(aifuture_)) {
                TOOLBOX_INFO << "address pending";
                return;
            }
            try {
                ep_ = ip_endpoint<Endpoint>(aifuture_);
            } catch (const std::exception& e) {
                TOOLBOX_ERROR << "failed to resolve address: " << e.what();
                aifuture_ = resolver_.resolve(uri_, SOCK_STREAM);
                return;
            }
            TOOLBOX_INFO << "address resolved: " << ep_;
        }
        TOOLBOX_INFO << "reconnecting";
        if (!connect(now, reactor_, ep_)) {
            inprogress_ = true;
        }
    }
    Reactor& reactor_;
    Resolver& resolver_;
    const std::string uri_;
    Timer tmr_;
    AddrInfoFuture aifuture_;
    Endpoint ep_;
    bool inprogress_{false};
    // List of active connections.
    ConnList conn_list_;
};

}
}
