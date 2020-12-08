#pragma once

#include "toolbox/io/Buffer.hpp"
#include "toolbox/io/Socket.hpp"
#include <aeron/aeronc.h>
#include <system_error>
#include <thread>
#include <toolbox/sys/Error.hpp>
#include <toolbox/util/Slot.hpp>

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


class AeronEndpoint {
public:
    AeronEndpoint(std::string channel, int32_t stream = 1)
    : channel_(std::move(channel))
    , stream_(stream) {}

    const std::string& channel() const { return channel_; }
    int32_t stream() const { return stream_; }
private:
    std::string channel_{};
    int32_t stream_{};
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

class AeronPoll : public Aeron {
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

    bool ctl(PollHandle& handle) {
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

template<typename SelfT>
class AeronPubAdd : public CompletionSlot<std::error_code>{
public:
    using Endpoint = typename SelfT::Endpoint;
    bool prepare(SelfT& self, const Endpoint& ep, Slot slot) {
        const char* ch = ep.channel().c_str();
        auto strm = ep.stream();
        if (aeron_async_add_exclusive_publication(
                &async_, self.aeron(), ch, strm) < 0) {
            std::error_code ec {::aeron_errcode(), std::system_category()};
            throw std::system_error{ec, ::aeron_errmsg()};
        }
        set_slot(slot);
        return false; //async
    }
    bool complete(SelfT& self) {
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
            self.set(pub);
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
public:
    using Buffer = ConstBuffer;
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
    aeron_async_add_exclusive_publication_t* add_ {};
    ConstBuffer buf_;
    ssize_t result_;
    std::error_code ec_;
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
    void async_connect(const AeronEndpoint& ep, Slot<std::error_code> slot) {
        connect_.prepare(*this, ep, slot);
    }

    // sync
    void connect(const AeronEndpoint& ep) {
        aeron_async_add_exclusive_publication_t* async {};
        if (aeron_async_add_exclusive_publication(
                &async, aeron_, ep.channel().data(), ep.stream()) < 0) {
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
    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {
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
            return position - prev_position;
        }
        ec = {(int)position, std::system_category()};
        return position;
    }
    aeron_exclusive_publication_t* get() { return pub_; }
    void set(aeron_exclusive_publication_t* pub) { pub_ = pub; }
    aeron_t* aeron() { return aeron_; } 
protected:
    PollHandle poll_;
    aeron_t* aeron_ {};
    aeron_exclusive_publication_t* pub_ {};
    AeronPubAdd<This> connect_; // async op
    AeronPubOffer<This> write_;
};



}
}