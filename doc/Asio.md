# Asio 

## Inspiration

### general IO library design
https://github.com/boostorg/asio/blob/develop/include/boost/asio/basic_stream_socket.hpp

### solarflare-related
polling abstraction https://github.com/MengRao/pollnet/blob/master/Efvi.h
optimized efvi udp md receiver+tcpdirect sender https://github.com/majek/openonload/blob/master/src/tests/trade_sim/trader_tcpdirect_ds_efvi.c#L152


## API example
```c++
#include <toolbox/io.hpp>
using namespace toolbox;
struct  HttpClientExample {
    using This = HttpClientExample; // see bind<>
    using Sock = StreamSockClnt; // use TCP stream socket client (see also StreamSockServ)
    // using Sock = TlsStreamSock<StreamSockClnt>;  // or will we use https:// ?
    using AsyncSock = StreamSocket<TcpSock>; // platform-agnostic sync+async socket
    
    BasicReactor<Epoll> r; // timers + pollers
    AsyncSock sock;
    Buffer buf;
    
    This() {
        Endpoint ep = parse_stream_endpoint("tcp4://127.0.0.1:8888");
        assert(ep.protocol()==StreamProtocol::v4());
        sock.open(r, ep.protocol());
        sock.async_connect(ep, [this](std::error_code ec) { // connection is ready
            if(ec) throw make_sys_error(ec);                // ... or failed
            sock.async_write("GET /foo HTTP/1.1\r\n\r\n", bind<&This::on_sent_request>(this));
        });
    }
    void on_sent_request(ssize_t bytes_tx, std::error_code ec) { // write complete
        if(ec) throw make_sys_error(ec);                         // ... or connection failed
        sock.async_read(buf.prepare(2397), bind<&This::on_got_response>(this)); // start read
    }
    void on_got_response(ssize_t bytes_rx, std::error_code ec) { // read complete
        if(ec)  throw make_sys_error(ec);                        // ... or failed
        std::cout << "recv: " << buf.str();
        if(!bytes_rx)
            sock.close();                                        // conn closed gracefully
        else
            sock.async_read(buf.prepare(2397), bind<&This::on_got_sesponse(this)>); // read more
    }
};
int main(){ This app; return 0; }
```
## Protocols
- Exchanges use UDP multicast with periodical snapshot as primary transport for market data. 

- Using unreliable UDP is due to the fact that strategies could often tolerate packet loss/reordering 
  - one could rebuild orderbook even if data is lost ["natural orderbook refresh"](https://www.cmegroup.com/confluence/display/EPICSANDBOX/MDP+3.0+-+Market+by+Price+-+Book+Recovery+Methods+for+Concurrent+Processing#MDP3.0MarketbyPriceBookRecoveryMethodsforConcurrentProcessing-BookRecovery-NaturalRefresh)

- If in-order reliable communication is absolutely required, one could use 
   - TCP (since it is optimized for decades, e.g. TcpDirect), 
   - [Aeron](https://github.com/real-logic/aeron/wiki/Transport-Protocol-Specification) - reliable UDP - uses NACKs instead os TCP ACKs.

- It is possible to design simple trading protocols tolerant to unreliable transport: 
  - packets reordering - via using sender-generated packet sequences - so receiver skips stale packets - and we don't care about PREVIOUS position
  - packet loss - receiver sends hearbeats(periodical ACKs) to sender with client's high seqeunce  - for sender to detect packet loss and resend 
  - sender sends periodical snapshots of market data - if client detected gap it recovers while continuing to receive updates.

- Last resort is TCP recovery - if we want history of messages in correct order - but it is not low latency - and required only for historical data collection purposes.

- In general, we should pass packets immediately to strategy logic. Tcp could wait too long while lost packets are resent

- Also: small packets are better - faster delivery, no fragmentation (should consider MTU size)

## Architecture

### Design highlights
- support kernel bypass for specific hardware (Intel DPDK, Solarflare EfVi, Solarflare TcpDirect, Mellanox VMA)
- support Epoll/Poll/IoUring on generic linux
- support zero-copy API when possible (zc_recv) when memory is not allocated by receiver at all, we look into DMA buffer
- support ipc shared memory ring buffers (nanoseconds latency)

### Sync sockets platform-specific layer
- Tcp/Pipes      ..... StreamSockClnt, StreamSockServ
- Udp connection ..... DgramSock
- Udp Multicast  ..... McastSock
- Tls/Ssl.       ..... TlsSockClnt<StreamSockClnt>, TlsSockServ<StreamSockServ> -- use https://github.com/h2o/picotls or OpenSSL
- Websocket      ..... WebSockClnt<StreamSockClnt>, WebSockServ<StreamSockServ> -- https://github.com/tatsuhiro-t/wslay or custom impl
- Ipc SpSc/MpSc/MpMc ..... MagicRingBuffer, MpMcQueue

### Poll platform-specific layer
#### standard linux:
- Epoll (linux) - for large number of connections with EPOLL_ET mode to avoid most of EPOLL_CTL_MOD
- Poll (linux)  - for small number of connections could have better latency since no EPOLL_CTL_MOD at all.
- IoUring (linux 5.x)
#### kernel bypass
- DPDK (intel, amazon)
- EfVI (solarflare, colocation only)
- TcpDirect (solarflare, colocation only)

### Sync+Async sockets platform-agnostic layer
- Stream(Tcp/Pipes)...  StreamSocket<Sock>
- UDP, UDP Multicast... DgramSocket<Sock>, McastSocket<Sock>

```
platform/hw API                 Sync/HW-specific layer                   Async layer.           Sync+Async API + zero-copy API
-------------------             ----------------------                   -------------          --------------------------------------
os::recv(buf), os::write(buf)   (Udp) McastSock, DgramSock -+                                +- socket.async_connect(endpoint, [](ec){})
                                (Tcp) StreamSock (posix)   -+-----------> Socket<Sock> ------+- socket.async_read(buf, [](size, ec){})
mrb::zc_recv(buf)               (Ipc) MagicRingBuf         -+                    ^           +- socket.async_write(buf, [](size, ec){})
sf::ef_vi_receive_post          (KByPass) EfViSock         -+                    |           +- socket.async_zc_recv([](buf, ec) {})
sf::zc_recv(zock)               (KByPass) TcpDirectSock    -+                    |
                                                                                 |
                                                                         on_io_event(fd, Read|Write)
                                                                                 | 
                poll API                 Pollers                                 |
               -------                   ---------                               |
Generic HW  -- os::epoll --------------- Epoll -------------+                    ^
Solarfale   -- sf::ef_eventq_poll ------ EfViPoll  ---------+                    |
X2522       -- sf::zf_reactor_perform -- TcpDirectPoll -----+---->----- Reactor -+
Intel       --- DPDK ------------------- /*TBD*/ -----------+     
Mellanox    ---- VMA ------------------- /*TBD*/------------+
ipc mmap    ---- SpSc/MpSc      -------- QPoll -------------+
```