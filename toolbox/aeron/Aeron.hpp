#pragma once

#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Socket.hpp"
#include "toolbox/net/Endpoint.hpp"
#include <aeron/aeronc.h>
#include "toolbox/net/ParsedUrl.hpp"
#include "toolbox/net/Protocol.hpp"
#include <system_error>
#include <thread>
#include <toolbox/sys/Error.hpp>
#include <toolbox/util/Slot.hpp>
#include <unordered_map>

namespace toolbox {
inline namespace aeron {

class AeronContext {
public:
    AeronContext(aeron_context_t* ctx)
    : ctx_ {ctx}
    {}
    
    AeronContext() = default;

    AeronContext(AeronContext&&) = default;
    AeronContext& operator=(AeronContext&&) = default;

    AeronContext(const AeronContext&) = delete;
    AeronContext& operator=(const AeronContext&) = delete;

    void open() {
        int rc = ::aeron_context_init(&ctx_);
        if(rc < 0) {
            throw std::system_error{make_sys_error(::aeron_errcode()), ::aeron_errmsg()};
        }
    }

    ~AeronContext() {
        if(ctx_) {
            ::aeron_context_close(ctx_);
            ctx_ = nullptr;
        }
    }
    aeron_context_t* get() { return ctx_; }
private:
    aeron_context_t* ctx_;
};



//aeron:udp?endpoint=192.168.0.1:40456|interface=192.168.0.3
class AeronEndpoint : protected net::ParsedUrl {
public:
    enum class EndpointType: uint8_t {
        Unknown,
        Udp,
        Ipc
    };
public: 
    AeronEndpoint() = default;

    AeronEndpoint(std::string_view s, int32_t stream)
    : ParsedUrl(s)
    , stream_(stream) {
    }

    EndpointType type() const {
        if(proto().find("udp")!=std::string_view::npos)
            return EndpointType::Udp;
        if(proto().find("ipc")!=std::string_view::npos)
            return EndpointType::Ipc;
        return EndpointType::Unknown;
    }
    
    std::string channel() const {
        return std::string {param("channel")};
    }

    int32_t stream() const {
        return stream_;
    }

    std::string_view interface() const {
        return param("interface");
    }
    
    UdpEndpoint udp_endpoint() {
        return parse_ip_endpoint<UdpEndpoint>(param("endpoint"));
    }
    McastEndpoint mcast_endpoint() {
       return parse_ip_endpoint<McastEndpoint>(param("endpoint"));
    }
private:
    int32_t stream_;
};

class Aeron {
public:
    Aeron(aeron_context_t* ctx=nullptr)
    : context_{ctx}
    {
        if(ctx)
            open(ctx);
    }

    void open(aeron_context_t* ctx) {
        context_ = ctx;
        if (aeron_init(&aeron_, context_) < 0) {
            throw std::system_error{make_sys_error(::aeron_errcode()), ::aeron_errmsg()};
        }
    }
    void start() {
        if (aeron_start(aeron_) < 0) {
            throw std::system_error{make_sys_error(::aeron_errcode()), ::aeron_errmsg()};
        }
    }
    void close() {
        if(aeron_)
            aeron_close(aeron_);
    }
    ~Aeron() {
        close();
    }
    aeron_t* get() { return aeron_; }
    aeron_context_t* context() { return context_; }
protected:
    aeron_t* aeron_{};
    aeron_context_t* context_;
};

class AeronPoll : public Aeron, virtual public IPoller {
  using Base = Aeron;
  public:
    constexpr static std::size_t FragmentCountLimit = 10;
    
    template<typename PollT>
    PollT& get() { return *this; }

    AeronPoll() {
        open();
    }

    void open() {
        context_.open();
        Base::open(context_.get());
        Base::start();
    }
    
    // TODO: find empty index
    int socket() const {
        return data_.size();
    }

    bool ctl(PollHandle& handle) override {
        std::size_t ix = handle.fd();
        if(ix >= data_.size()) {
            data_.resize(ix+1);
        }
        data_[ix] = handle.slot();
        return true;
    }

    void open(AeronContext&& ctx) {
        context_ = std::move(ctx);
        Aeron::open(context_.get());
    }
    
    int wait(std::error_code& ec) noexcept {        
        int ix = 0;
        int work = 0;
        for(auto& slot: data_) {
            if(slot) {
                slot.invoke(CyclTime::now(), ix, PollEvents::Read + PollEvents::Write);
                work++;
            }
            ix++;
        }
        return work;
    }

    int wait(MonoTime timeout, std::error_code& ec) noexcept {
         // TODO: aeron_controlled_poll
         return wait(ec);
    }
    int dispatch(CyclTime now) { return 0; }

    aeron_t* aeron() { return Aeron::get(); }
  private:
    std::vector<IoSlot> data_;
    AeronContext context_;
};


class Aeron;

using AeronFragmentSlot = Slot<void*, const uint8_t*,size_t, aeron_header_t*>;

template<typename SocketT>
class AeronPubAdd : public CompletionSlot<std::error_code> {
    using Base = CompletionSlot<std::error_code>;
public:
    using Endpoint = typename SocketT::Endpoint;
    using typename Base::Slot;
    bool prepare(SocketT& socket, const Endpoint& ep, Slot slot) {
        const char* ch = ep.channel().data();
        auto strm = ep.stream();
        if (aeron_async_add_exclusive_publication(
                &async_, socket.aeron(), ch, strm) < 0) {
            std::error_code ec {::aeron_errcode(), std::system_category()};
            throw std::system_error{ec, ::aeron_errmsg()};
        }
        set_slot(slot);
        return false; //async
    }
    bool complete(SocketT& socket) {
        aeron_exclusive_publication_t* pub{};
        int res = aeron_async_add_exclusive_publication_poll(&pub, async_);
        if (res < 0) {
            async_ = nullptr;
            std::error_code ec {::aeron_errcode(), std::system_category()};
            notify(ec);
            return true; // done
        }
    
        if(pub!=nullptr) {
            async_ = nullptr;
            std::error_code ec {};
            socket.pub(pub);
            notify(ec);
            return true;
        }
        return false; // continue poll
    }
private:
    aeron_async_add_exclusive_publication_t* async_ {};
};

inline bool aeron_again(ssize_t retval) {
    if(retval>=0)
        return false;
    switch(retval) {
        case AERON_PUBLICATION_CLOSED:
        case AERON_PUBLICATION_ERROR:
            return false;
        case AERON_PUBLICATION_NOT_CONNECTED:
        case AERON_PUBLICATION_ADMIN_ACTION:
        case AERON_PUBLICATION_BACK_PRESSURED:        
            return true;
        default: // EAGAIN
            return true;
    }
}

template<typename SelfT>
class AeronPubOffer : public CompletionSlot<ssize_t, std::error_code> {
    using Base = CompletionSlot<ssize_t, std::error_code>;
public:
    using Buffer = ConstBuffer;
    using typename Base::Slot;
    bool prepare(SelfT& self, const Buffer& buf, Slot slot) {
        buf_ = buf;
        set_slot(slot);
        return false;
    }
    bool complete(SelfT& self) {
        std::error_code ec{};
        auto size =  self.write(buf_.data(), buf_.size(), ec);
        if(aeron_again(size))
            return false;
        notify(size, ec);
        return true; // done
    }
private:
    Buffer buf_;
};

class AeronPub {
public:
    using Endpoint = AeronEndpoint;
    using This = AeronPub;
public:
    AeronPub() = default;

    template<typename ReactorT>
    AeronPub(ReactorT& r)
    : poll_{r.poll(r.template get<AeronPoll>().socket())}
    , aeron_(r.template get<AeronPoll>().aeron()) {
        poll_.add(PollEvents::Read, bind<&This::on_io_event>(this));
        poll_.commit();
    }
    
    AeronPub(const AeronPub&) = delete;
    AeronPub& operator=(const AeronPub&) = delete;

    AeronPub(AeronPub&&) = default;
    AeronPub& operator=(AeronPub&&) = default;

    ~AeronPub() {
        close();
    }
    template<typename ReactorT>
    void open(ReactorT& r) {
        poll_ = r.poll(r.template get<AeronPoll>.socket());
        aeron_ = r.template get<AeronPoll>().aeron();
        poll_.add(PollEvents::Read, bind<&This::on_io_event>(this));
        poll_.commit();
    }
    void close() {
        if(pub_) {
            aeron_exclusive_publication_close(pub_, nullptr, nullptr);
        }
    }
    bool empty() { return pub_==nullptr; }

    // async
    void async_connect(const Endpoint& ep, Slot<std::error_code> slot) {
        connect_.prepare(*this, ep, slot);
    }

    // sync
    void connect(const Endpoint& ep) {
        aeron_async_add_exclusive_publication_t* async {};
        if (aeron_async_add_exclusive_publication(
                &async, aeron_, ep.channel().c_str(), ep.stream()) < 0) {
            std::error_code ec {::aeron_errcode(), std::system_category()};
            throw std::system_error{ec, ::aeron_errmsg()};
        }        
        while(pub_==nullptr) {
            if (aeron_async_add_exclusive_publication_poll(&pub_, async) < 0) {
                std::error_code ec {::aeron_errcode(), std::system_category()};
                throw std::system_error{ec, ::aeron_errmsg()};
            }        
        }
    }

    // TODO: refactor into on_poll(PollFD& fd)
    void on_io_event(CyclTime now, int fd, PollEvents events) {
        if(connect_) {
            connect_.complete(*this);
            return;
        }
        // do write_ while we can
        while(write_) {
            if(!write_.complete(*this)) {
                std::this_thread::yield(); // back pressure?
                break;
            }
        }
    }

    void async_write(ConstBuffer buf, Slot<ssize_t, std::error_code> slot) {
        write_.prepare(*this, buf, slot);
    }

    ssize_t write(const void* buf, std::size_t size, std::error_code &ec) {
        ssize_t prev_position = aeron_exclusive_publication_position(pub_);
        ssize_t position = aeron_exclusive_publication_offer(
            pub_, static_cast<const uint8_t*>(buf), size, nullptr, nullptr);
        if(position>=0) {
            ec = {};
            ssize_t dpos = position - prev_position;
            assert((ssize_t)size<=dpos);
            return size;
        }
        ec = {(int)position, std::system_category()};
        return position;
    }
    aeron_exclusive_publication_t* pub() { return pub_; }
    void pub(aeron_exclusive_publication_t* pub) { pub_ = pub; }
    aeron_t* aeron() { return aeron_; } 
protected:
    PollHandle poll_;
    aeron_t* aeron_ {};
    aeron_exclusive_publication_t* pub_ {};
    AeronPubAdd<This> connect_; // async op
    AeronPubOffer<This> write_;
};

template<typename SocketT>
class AeronSubAdd : public CompletionSlot<std::error_code> {
    using Base = CompletionSlot<std::error_code>;
public:
    using Endpoint = typename SocketT::Endpoint;
    using typename Base::Slot;

    constexpr static aeron_on_available_image_t avail_image = nullptr;
    constexpr static void *avail_image_data = nullptr; 
    constexpr static aeron_on_unavailable_image_t unavail_image = nullptr;
    constexpr static void *unavail_image_data = nullptr;

    bool prepare(SocketT& socket, const Endpoint& ep, Slot slot) {
        const char* ch = ep.channel().c_str();
        auto strm = ep.stream();
        if (aeron_async_add_subscription(
                &async_, socket.aeron(), ch, strm, 
                avail_image, avail_image_data, 
                unavail_image, unavail_image_data) <0) {
            std::error_code ec {::aeron_errcode(), std::system_category()};
            throw std::system_error{ec, ::aeron_errmsg()};
        }
        set_slot(slot);
        return false; //async
    }
    bool complete(SocketT& socket) {
        aeron_subscription_t* sub{};
        int res = aeron_async_add_subscription_poll(&sub, async_);
        if (res < 0) {
            async_ = nullptr;
            std::error_code ec {::aeron_errcode(), std::system_category()};
            notify(ec);
            return true; // done
        }
    
        if(sub!=nullptr) {
            async_ = nullptr;
            std::error_code ec {};
            socket.sub(sub);
            notify(ec);
            return true;
        }
        return false; // continue poll
    }
private:
    aeron_async_add_exclusive_publication_t* async_ {};
};


template<typename SocketT>
class AeronSubPoll : public CompletionSlot<ssize_t, std::error_code> {
    using Base = CompletionSlot<ssize_t, std::error_code>;
public:
    using Buffer = ConstBuffer;
    using typename Base::Slot;

    bool prepare(SocketT& socket, const Buffer& buf, Slot slot) {
        buf_ = buf;
        set_slot(slot);
        return false;
    }
    bool complete(SocketT& socket) {
        std::error_code ec{};
        auto size =  socket.write(buf_.data(), buf_.size(), ec);
        if(aeron_again(size))
            return false;
        notify(size, ec);
        return true; // done
    }
private:
    aeron_async_add_exclusive_publication_t* add_ {};
    ConstBuffer buf_;
    ssize_t result_;
    std::error_code ec_;
};

template<typename SocketT>
class AeronSubRead : public CompletionSlot<ssize_t, std::error_code> {
    using Base = CompletionSlot<ssize_t, std::error_code>;
    using This = AeronSubRead<SocketT>;
public:
    using Buffer = MutableBuffer;
    using typename Base::Slot;
    
    constexpr static int FragmentCountLimit = 10;

    ~AeronSubRead() {
        close();
    }
    void open() {
        if (aeron_fragment_assembler_create(&frag_asm_, aeron_read_handler, this) < 0) {
            std::error_code ec {::aeron_errcode(), std::system_category()};
            throw std::system_error{ec, ::aeron_errmsg()};
        }
    }
    void close() {
        if(frag_asm_) {
            aeron_fragment_assembler_delete(frag_asm_);
            frag_asm_ = nullptr;
        }
    }
    bool prepare(SocketT& socket, const Buffer& buf, Slot slot) {
        buf_ = buf;
        set_slot(slot);
        return false;
    }
    template<bool with_notify=true>
    bool complete(SocketT& socket) {
        size_ = 0;
        ssize_t nfrags = aeron_subscription_poll(
            socket.sub(), aeron_fragment_assembler_handler, frag_asm_, FragmentCountLimit);
        if (nfrags>0) {
            assert(size_>=0);
            ec_ = {};
            if constexpr(with_notify)
                notify(size_, ec_);
            return true;
        } else if (nfrags < 0) {
            ec_ = {::aeron_errcode(), std::system_category()};
            if constexpr(with_notify)
                notify(nfrags, ec_);
            return true;
        }
        return false; // nflags==0
    }
    void on_read(const uint8_t* buffer, size_t len, aeron_header_t* header) {
        auto size = std::min(len, buf_.size());
        std::memcpy(buf_.data(), buffer, size);
        size_ = size;
    }
    static void aeron_read_handler(void *d, const uint8_t *buffer, size_t length, aeron_header_t *header) {
        This* self = static_cast<This*>(d);
        self->on_read(buffer, length, header);
    }
    ssize_t bytes_transferred() { return size_; }
    std::error_code& get_error() { return ec_; }
private:
    aeron_async_add_exclusive_publication_t* add_ {};
    Buffer buf_;
    std::error_code ec_ {};
    ssize_t size_ {};
    aeron_fragment_assembler_t* frag_asm_{};
};

class AeronSub {
public:
    using Endpoint = AeronEndpoint;
    using This = AeronSub;
public:
    AeronSub() = default;

    template<typename ReactorT>
    AeronSub(ReactorT& r)
    : poll_{r.poll(r.template get<AeronPoll>().socket())}
    , aeron_(r.template get<AeronPoll>().aeron()) {
        poll_.add(PollEvents::Read, util::bind<&This::on_io_event>(this));
        poll_.commit();
    }
    
    AeronSub(const AeronSub&) = delete;
    AeronSub& operator=(const AeronSub&) = delete;

    AeronSub(AeronSub&&) = default;
    AeronSub& operator=(AeronSub&&) = default;

    ~AeronSub() {
        close();
    }
    template<typename ReactorT>
    void open(ReactorT& r) {
        poll_ = r.poll(r.template get<AeronPoll>.socket());
        aeron_ = r.template get<AeronPoll>().aeron();
        poll_.add(PollEvents::Write, util::bind<&This::on_io_event>(this));
        poll_.commit();
    }
    void close() {
        if(sub_) {
            aeron_subscription_close(sub_, nullptr, nullptr);
        }
    }
    bool empty() { return sub_==nullptr; }

    // async
    void async_bind(const Endpoint& ep, Slot<std::error_code> slot) {
        bind_.prepare(*this, ep, slot);
    }

    // sync
    void bind(const Endpoint& ep) {
        aeron_async_add_subscription_t* async {};
        if (aeron_async_add_subscription(
                &async, aeron_, ep.channel().data(), ep.stream(), 
                bind_.avail_image, bind_.avail_image_data, bind_.unavail_image, bind_.unavail_image_data) < 0) {
            std::error_code ec {::aeron_errcode(), std::system_category()};
            throw std::system_error{ec, ::aeron_errmsg()};
        }        
        while(sub_==nullptr) {
            if (aeron_async_add_subscription_poll(&sub_, async) < 0) {
                std::error_code ec {::aeron_errcode(), std::system_category()};
                throw std::system_error{ec, ::aeron_errmsg()};
            }        
        }
        read_.open();
    }

    // TODO: refactor into on_poll(PollFD& fd)
    void on_io_event(CyclTime now, int fd, PollEvents events) {
        if(bind_) {
            bind_.complete(*this);
            return;
        }
        // do write_ while we can
        while(read_) {
            if(!read_.template complete<true>(*this)) {
                break;
            }
        }
    }

    void async_read(MutableBuffer buf, Slot<ssize_t, std::error_code> slot) {
        read_.prepare(*this, buf, slot);
    }

    ssize_t read(MutableBuffer buf, std::error_code& ec) {
        read_.prepare(*this, buf, nullptr);
        if(read_.complete<false>(*this)) {
            ec = read_.get_error();
            return read_.bytes_transferred();
        }
    }
    ssize_t read(void* buf, std::size_t size, std::error_code &ec) {
        return read(MutableBuffer(buf, size), ec);
    }

    

    aeron_subscription_t* sub() { return sub_; }
    void sub(aeron_subscription_t* sub) { 
        sub_ = sub; 
        read_.open();
    }
    aeron_t* aeron() { return aeron_; } 
protected:
    PollHandle poll_;
    aeron_t* aeron_ {};
    aeron_subscription_t* sub_ {};
    AeronSubAdd<This> bind_; // async op
    AeronSubRead<This> read_;
};



} //aeron
} // toolbox
namespace toolbox {
inline namespace net {
    template<>
    struct EndpointTraits<AeronEndpoint> {
        UdpEndpoint to_udp(AeronEndpoint& ep) {
            return ep.udp_endpoint();
        }
        McastEndpoint to_mcast(AeronEndpoint& ep) {
            return ep.mcast_endpoint();
        }
    };
} // net
} // toolbox