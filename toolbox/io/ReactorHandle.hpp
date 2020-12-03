#pragma once

#include "toolbox/io/Waker.hpp"
#include <cstdint>
#include <stdexcept>
#include <toolbox/io/Handle.hpp>
#include <toolbox/util/Enum.hpp>
#include <toolbox/io/Scheduler.hpp>
namespace toolbox {
inline namespace io {

class IReactor;

enum PollEvents: uint32_t {
    None        = 0,
    Read        = 1,
    Write       = 4,
    Error       = 8,
    ET          = 1U << 31
};

constexpr inline PollEvents operator+(PollEvents lhs, PollEvents rhs) {
    return static_cast<PollEvents>(unbox(lhs) | unbox(rhs));
}

constexpr inline PollEvents operator-(PollEvents lhs, PollEvents rhs) {
    return static_cast<PollEvents>(unbox(lhs) & ~unbox(rhs));
}

inline std::ostream& operator<<(std::ostream& os, PollEvents val) {
    os << "\"";
    if(val & PollEvents::Read)
        os << " Read";
    if(val & PollEvents::Write)
        os <<" Write";
    os << "\"";
    return os;
}

struct FDS {
    int fd;     // linux file handle
    int sid;
};

union FDU {
    FDS fds;
    void *ptr;  // arbitrary object
};

// FDS or ptr
class PollFD {
public:
    explicit PollFD(int fd, int sid = 0) {
        data_.fds.fd = fd;
        data_.fds.sid = sid;
    }

    explicit PollFD(void* ptr) {
        data_.ptr = ptr;
    }

    constexpr PollFD(std::nullptr_t=nullptr) noexcept {};

    void swap(PollFD &rhs) {
        std::swap(data_, rhs.data_);
    }

    void reset() {
        data_.fds.fd = -1;
        data_.fds.sid = 0;
    }

    int fd() const noexcept { return data_.fds.fd; }
    int sid() const noexcept { return data_.fds.sid; }
    void sid(int sid) noexcept { data_.fds.sid = sid;}
    void* ptr() const noexcept { return data_.ptr; }
protected:
    FDU data_ {};
};

using IoSlot = BasicSlot<CyclTime, os::FD, PollEvents>;

// RAI-style wrapper around PollFD
class PollHandle : public PollFD {
public:
    using Base = PollFD;

    constexpr PollHandle(std::nullptr_t = nullptr) noexcept {}
    
    PollHandle(IReactor* reactor, os::FD fd, int sid)
    : Base(fd, sid)
    , reactor_(reactor)
    {}
    
    ~PollHandle() { reset(); }

    // Copy.
    PollHandle(const PollHandle&) = delete;
    PollHandle& operator=(const PollHandle&) = delete;

    // Move.
    PollHandle(PollHandle&& rhs) noexcept
    : Base(rhs)
    , reactor_{rhs.reactor_}
    {
        rhs.reactor_ = nullptr;
        rhs.Base::reset();
    }
    PollHandle& operator=(PollHandle&& rhs) noexcept
    {
        reset();
        swap(rhs);
        return *this;
    }

    void reset(std::nullptr_t = nullptr) noexcept;
    void swap(PollHandle& rhs) noexcept {
        std::swap(reactor_, rhs.reactor_);
        Base::swap(rhs);
    }
    bool empty() const noexcept { return reactor_ == nullptr; }
    explicit operator bool() const noexcept { return reactor_ != nullptr; }

    void resubscribe(PollEvents events);
    void resubscribe(PollEvents events, IoSlot slot);
    IReactor* reactor() { return reactor_; }
protected:
    IReactor *reactor_ {nullptr};
};


class IReactor {
public:
    virtual ~IReactor() = default;
    virtual void subscribe(PollHandle& handle, PollEvents events, IoSlot slot) = 0;
    virtual void unsubscribe(PollHandle& handle) = 0;
    virtual void resubscribe(PollHandle& handle, PollEvents events) = 0;    
    virtual void resubscribe(PollHandle& handle, PollEvents events, IoSlot slot) = 0;    
};

template<typename ImplT>
class ReactorImpl : public ImplT, public IReactor {
public:
    using Base=ImplT;
    using Base::Base;
    using Base::wakeup;
    using Base::wait;
    using Base::dispatch;
public:
    void subscribe(PollHandle& handle, PollEvents events, IoSlot slot) override { Base::subscribe(handle, events, slot); };
    void unsubscribe(PollHandle& handle) override { return Base::unsubscribe(handle); };
    void resubscribe(PollHandle& handle, PollEvents events) override { Base::resubscribe(handle, events); };    
    void resubscribe(PollHandle& handle, PollEvents events, IoSlot slot) override { Base::resubscribe(handle, events, slot); };    
};

inline void PollHandle::reset(std::nullptr_t) noexcept
{
    if (reactor_) {
        reactor_->unsubscribe(*this);
        reactor_ = nullptr;
        //FIXME: move to unsubscribe
        Base::reset();
    }
}

inline void PollHandle::resubscribe(PollEvents events) {
    reactor_->resubscribe(*this, events);
} 

inline void PollHandle::resubscribe(PollEvents events, IoSlot slot) {
    reactor_->resubscribe(*this, events, slot);
} 


}
}