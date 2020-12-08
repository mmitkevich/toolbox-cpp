#include "toolbox/aeron/AeronSocket.hpp"
#include "toolbox/util/Slot.hpp"
#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Reactor.hpp"

namespace tb = toolbox;

template<typename ReactorT>
class AeronServ {
  using This = AeronServ;
  public:

    AeronServ(ReactorT& r)
    : socket_{r}
    , reactor_{r}
    {
        tb::AeronEndpoint ep ("aeron:udp?endpoint=localhost:20123", 1002);
        socket_.async_connect(ep, tb::bind<&This::on_connected>(this));
    }
    void on_connected(std::error_code ec) {
      TOOLBOX_INFO << "connected, ec:"<<ec;
      send();      
    }
    void send() {
      auto s = std::string_view {"Hello"};
      tb::ConstBuffer buf = {s.data(), s.size()};
      socket_.async_write(buf, tb::bind<&This::on_sent>(this));
    }
    void on_sent(ssize_t size, std::error_code ec) {
      TOOLBOX_INFO << "sent, ec:"<<ec<<", size:"<<size;
      if(!ec) {
        using namespace std::chrono_literals;
        tim_ = reactor_.timer(tb::CyclTime::now().mono_time() + 5s, tb::Priority::High, 
          tb::bind<&This::on_timer>(this)
        );
      }
    }
    void on_timer(tb::CyclTime now, tb::Timer& tmr) {
      send();
    }
    void on_recv(ssize_t size, std::error_code ec) {
      TOOLBOX_INFO << "recv, ec:"<<ec<<", size:"<<size;
    }
  protected:
    tb::AeronSocket socket_ {};
    tb::Buffer buf_;
    tb::Timer tim_;
    ReactorT& reactor_;
    
};

int main() {
    tb::BasicReactor<tb::AeronPoll> reactor;
    AeronServ serv {reactor};
    reactor.run();
    return 0;
}