#include "toolbox/io/Reactor.hpp"
#include "toolbox/io/Runner.hpp"
#include "toolbox/net/Endpoint.hpp"
#include "toolbox/net/StreamSock.hpp"
#include "toolbox/io/StreamSocket.hpp"
#include "toolbox/sys/Log.hpp"
#include "toolbox/sys/Time.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <system_error>
#include <toolbox/io.hpp>
#include <toolbox/sys.hpp>
#include <toolbox/util.hpp>

using namespace std::string_literals;
using namespace toolbox;
namespace tb = toolbox;

class HttpClnt2 {
  public:
    using Socket = StreamSocket;
    using Endpoint = typename Socket::Endpoint;
    using Protocol = typename Socket::Protocol;
    using This = HttpClnt2;
  public:
    HttpClnt2(tb::IReactor& r)
    : reactor_(r) 
    {}
    
    void open(const Endpoint& ep) {
        endpoint_ = ep;
        sock_.open(ep.protocol(), reactor_);
        sock_.connect(ep, tb::bind<&This::on_connected>(this));
    }
    void close() {
        TOOLBOX_DEBUG<<"close";
        sock_.close();
    }

    void stop() {
        close();
        TOOLBOX_DEBUG<<"stop";
        reactor_.stop();
    }
    
    static constexpr std::string_view Request = "GET /foo HTTP/1.1\r\n\r\n";

    void on_connected(std::error_code ec) {
        TOOLBOX_DEBUG<<"connected, ep:" <<endpoint_<<", ec:"<<ec;
        if(ec) {
            stop();
        }
        send(Request);
    }

    void send(std::string_view req) {
        TOOLBOX_DEBUG<<count_<<" send: " << req;
        auto buf = output_.prepare(req.size());
        std::memcpy(buf.data(), req.data(), req.size());
        sock_.write({buf.data(), req.size()}, tb::bind<&This::on_sent>(this));
    }

    void on_sent(ssize_t size, std::error_code ec) {
        assert(size>=0);
        output_.commit(size);
        output_.consume(size);
        TOOLBOX_INFO<<count_<<" sent: " << size <<" ec:"<<ec;
        sock_.read(input_.prepare(2397), tb::bind<&This::on_recv>(this));
    }
    void on_recv(ssize_t size, std::error_code ec) {
        assert(size>=0);
        input_.commit(size);
        
        TOOLBOX_INFO << count_<<" recv: " << size <<" ec:"<<ec<<" " << std::string_view {(const char*)input_.buffer().data(), input_.buffer().size()};
        input_.consume(size);
        if(count_<100) {
            count_++;            
            send(Request);
        } else if(size==0) { // EOF
            stop();
        } else {
            sock_.read(input_.prepare(2397), tb::bind<&This::on_recv>(this));
        }
        //sock_.read(input_.prepare(2397), tb::bind<&This::on_recv>(this));
    }
  private:
    Socket sock_;
    tb::IReactor& reactor_;
    Endpoint endpoint_;    
    Buffer input_;
    Buffer output_;
    std::size_t count_{};
};

int main(int argc, char* argv[]) {
    tb::ReactorImpl<tb::Reactor> reactor{1024};
    HttpClnt2 clnt(reactor);
    auto ep = tb::parse_stream_endpoint("tcp4://127.0.0.1:8888");
    clnt.open(ep);
    reactor.run();
    
    return EXIT_SUCCESS;
}