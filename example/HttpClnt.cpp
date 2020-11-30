#include "toolbox/io/Runner.hpp"
#include "toolbox/net/Endpoint.hpp"
#include "toolbox/net/StreamSock.hpp"
#include "toolbox/net/AsyncStreamSock.hpp"
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

class HttpClnt {
  public:
    using Socket = AsyncStreamSock<StreamSockClnt>;
    using Endpoint = typename Socket::Endpoint;
    using Protocol = typename Socket::Protocol;
    using This = HttpClnt;
  public:
    HttpClnt(tb::Reactor& reactor)
    : sock_(reactor) {

    }
    void open(Protocol protocol) {
        sock_.open(protocol);
    }
    void connect(const Endpoint& ep) {
        endpoint_ = ep;
        open(ep.protocol());
        sock_.connect(ep, tb::bind<&This::on_connected>(this));
    }
    
    static constexpr std::string_view Req = "GET /foo HTTP/1.1\r\n\r\n";

    void on_connected(std::error_code ec) {
        TOOLBOX_DEBUG<<"connected, ep:" <<endpoint_<<", ec:"<<ec;
        if(ec) {
            throw std::system_error(ec);
        }
        send(Req);
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
        TOOLBOX_DEBUG<<count_<<" sent: " << size <<" ec:"<<ec;
        sock_.read(input_.prepare(2397), tb::bind<&This::on_recv>(this));
    }
    void on_recv(ssize_t size, std::error_code ec) {
        assert(size>=0);
        input_.commit(size);
        TOOLBOX_DEBUG << count_<<" recv: " << size <<" ec:"<<ec<<" " << std::string_view {(const char*)input_.buffer().data(), input_.buffer().size()};
        input_.consume(size);
        if(count_<100) {
            count_++;            
            send(Req);
        }
        //sock_.read(input_.prepare(2397), tb::bind<&This::on_recv>(this));
    }
  private:
    Endpoint endpoint_;
    Socket sock_;
    Buffer input_;
    Buffer output_;
    std::size_t count_{};
};

int main(int argc, char* argv[]) {
    tb::Reactor reactor{1024};
    auto ep = tb::parse_stream_endpoint("tcp4://127.0.0.1:8888");
    auto clnt = HttpClnt(reactor);
    clnt.connect(ep);

    //auto timer_sub = reactor.timer(CyclTime::now().mono_time(), Duration::zero(), Priority::Low, bind(&on_timer));
    reactor.run();
    
    return EXIT_SUCCESS;
}