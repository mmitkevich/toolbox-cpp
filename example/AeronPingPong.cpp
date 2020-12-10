#include "toolbox/aeron/Aeron.hpp"
#include "toolbox/aeron/AeronSocket.hpp"
#include "toolbox/sys/Time.hpp"
#include "toolbox/util/Slot.hpp"
#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Reactor.hpp"
#include <sstream>
namespace tb = toolbox;

template<typename ReactorT>
class AeronPingPong {
  using This = AeronPingPong;
  public:

    AeronPingPong(ReactorT& r)
    : socket_{r}
    , reactor_{r}
    { }

    ~AeronPingPong() {
        close();
    }

    void interval(tb::Duration interval) {
      interval_ = interval;
    }

    void open(tb::AeronEndpoint local, tb::AeronEndpoint remote) {
      remote_ = remote;
      local_ = local;
      socket_.async_bind(local_, tb::bind<&This::on_bind>(this));
    }

    void close() {
      socket_.close();
    }

    void on_bind(std::error_code ec) {
        TOOLBOX_INFO<<"bind, ec:"<<ec;
        if(!ec)
          socket_.async_connect(remote_, tb::bind<&This::on_connected>(this));
    }

    void on_connected(std::error_code ec) {
      TOOLBOX_INFO << "connected, ec:"<<ec;
      if(!ec)
        do_write();
    }
    
    std::string payload;
    std::size_t i{0};

    void do_write() {
      std::stringstream ss;
      ss << "Ping-" << (++i);
      payload = ss.str();
      socket_.async_write(tb::as_const_buffer( payload ), tb::bind<&This::on_sent>(this));
      do_read();
    }

    void do_read() {
      socket_.async_read(buf_.prepare(4096), tb::bind<&This::on_read>(this));
    }

    void on_sent(ssize_t size, std::error_code ec) {
      TOOLBOX_INFO << "sent, ec:"<<ec<<", size:"<<size<<", "<<payload;
      if(!ec) {
        tim_ = reactor_.timer(tb::CyclTime::now().mono_time() + interval_, tb::Priority::High, 
          tb::bind<&This::on_timer>(this)
        );
      }
    }

    void on_read(ssize_t size, std::error_code ec) {
      buf_.commit(size);
      TOOLBOX_INFO << "read, ec:"<<ec<<", size:"<<size<<", "<<buf_.str();
      buf_.consume(size);
      do_read();
    }

    void on_timer(tb::CyclTime now, tb::Timer& tmr) {
      do_write();
    }

  protected:
    tb::AeronSocket socket_ {};
    tb::Buffer buf_;
    tb::Timer tim_;
    ReactorT& reactor_;
    tb::AeronEndpoint local_, remote_;
    tb::Duration interval_ {};
};

int main() {
    using namespace std::chrono_literals;
    auto local = tb::AeronEndpoint("aeron:udp?endpoint=localhost:20123", 1002);
    auto remote = tb::AeronEndpoint("aeron:udp?endpoint=localhost:20123", 1002);

    tb::BasicReactor<tb::AeronPoll> reactor;
    AeronPingPong pingpong {reactor};
    pingpong.interval(1s);
    pingpong.open(local, remote);
    reactor.run();

    return EXIT_SUCCESS;
}