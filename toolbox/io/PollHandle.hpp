#pragma once

#include "toolbox/io/Waker.hpp"
#include <cstdint>
#include <stdexcept>
#include <toolbox/io/Handle.hpp>
#include <toolbox/util/Enum.hpp>
#include <toolbox/io/Scheduler.hpp>
#include <toolbox/sys/Log.hpp>

namespace toolbox {
inline namespace io {


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
        os << " Write";
    if(val & PollEvents::ET)
        os<<" ET";
    if(!val)
        os<<" None";
    os << "\"";
    return os;
}

using IoSlot = BasicSlot<CyclTime, os::FD, PollEvents>;

// FDS or ptr
class PollFD {
public:
    using FD = os::FD;
    using SID = std::int32_t;
    struct FDS {
        os::FD fd;     // linux file handle
        std::int32_t sid;
    };

    explicit PollFD(FD fd) {
        data_.fds.fd = fd;
        data_.fds.sid = 0;
    }

    explicit PollFD(void* ptr) {
        data_.ptr = ptr;
    }

    explicit constexpr PollFD(std::nullptr_t=nullptr) noexcept {};

    // Copy.
    PollFD(const PollFD&) = default;
    PollFD& operator=(const PollFD&) = default;

    // Move.
    PollFD(PollFD&& rhs) noexcept = default;
    PollFD& operator=(PollFD&& rhs) noexcept = default;

    ~PollFD() {
        reset();
    }

    void swap(PollFD &rhs) {
        std::swap(data_, rhs.data_);
        std::swap(events_, rhs.events_);
        std::swap(slot_, rhs.slot_);
    }

    void reset() {
        data_.fds.fd = -1;
        data_.fds.sid = 0;
        events_ = PollEvents::None;
        slot_.reset();
    }

    os::FD fd() const noexcept { return data_.fds.fd; }
    void fd(int fd) noexcept { data_.fds.fd = fd; }

    int sid() const noexcept { return data_.fds.sid; }
    int next_sid() noexcept { return ++data_.fds.sid;}
    
    void* ptr() const noexcept { return data_.ptr; }

    bool empty() const noexcept { return slot_.empty(); }
    explicit operator bool() const noexcept { return !empty(); }

    PollEvents events() const noexcept { return events_; }
    void events(PollEvents events) noexcept { events_ = events; }
    
    IoSlot slot() const noexcept { return slot_; }
    void slot(IoSlot slot) noexcept { slot_ = slot; }
protected:
    union Data {
        FDS fds;
        void *ptr;  // arbitrary object
    } data_{};
    PollEvents events_ {PollEvents::None};
    IoSlot slot_ {};
};

class PollHandle;

using PollSlot = Slot<PollHandle&>;

// PollHandle is interface to reactor
class PollHandle : public PollFD {
public:
    using Base = PollFD;

    using Base::Base;
    explicit constexpr PollHandle(std::nullptr_t = nullptr) noexcept {}
    
    explicit PollHandle(os::FD fd, PollSlot ctl)
    : Base(fd)
    , ctl_(ctl)
    {}

    explicit PollHandle(PollSlot ctl)
    : ctl_(ctl)
    {}
    
    ~PollHandle() { 
        reset();
    }

    // Copy.
    PollHandle(const PollHandle&) = delete;
    PollHandle& operator=(const PollHandle&) = delete;

    // Move.
    PollHandle(PollHandle&& rhs) noexcept
    : Base(rhs)
    , ctl_{rhs.ctl_} {
        rhs.ctl_ = nullptr;
        rhs.Base::reset();
    }
    PollHandle& operator=(PollHandle&& rhs) noexcept {
        reset();
        swap(rhs);
        return *this;
    }
    void swap(PollHandle& rhs) noexcept {
        Base::swap(rhs);
        std::swap(ctl_, rhs.ctl_);
    }

    /// closes subscription
    void reset(std::nullptr_t=nullptr) noexcept {
        if(slot_!=nullptr || events_!=PollEvents::None) {
            events_ = PollEvents::None;
            slot_ = nullptr;
            commit();
        }
        Base::reset();
    }

    // remove events, keep slot
    void del(PollEvents events) noexcept {
        if(events_ & events) {
            events_ = events_ - events;
            //commit();
            pending_commit_ = true;
        }
    }

    /// remove events, change slot
    void del(PollEvents events, IoSlot slot) noexcept {
        if((events_ & events) || (slot != slot_)) {
            events_ = events_ - events;
            slot_ = slot;
            //commit();
            pending_commit_ = true;
        }
    }
    [[noinline]]
    void commit() noexcept {
        // possible optimize more with constexpr if (epoll.is_et())....
        if(pending_commit_) {
            //TOOLBOX_DEBUG<<"poll_commit "<<events_;
            ctl_(*this);
            pending_commit_ = false;
        }
    }

    /// adds events, keep slot
    void add(PollEvents events) noexcept {
        if((events_ & events) != events) {
            events_ = events_ + events;
            if(slot_)
                pending_commit_ = true;
        }
    }
    /// adds events, change slot
    void add(PollEvents events, IoSlot slot) noexcept {
        if(slot!=slot_ || (events_ & events) != events) {
            slot_ = slot;
            events_ = events_ + events;
            pending_commit_ = true;
        }
    }

/* 
    /// change events
    void mod(PollEvents events) {
        if(events_!=events) {
            events_ = events;
            pending_commit_ = true;
        }
    }
    /// change events and slot
    void mod(PollEvents events, IoSlot slot) {
        if(events_!=events || slot!=slot_) {
            slot_ = slot;
            events_ = events;
            pending_commit_ = true;
        }
    }
*/
    /// change slot
    void mod(IoSlot slot) noexcept {
        if(slot_!=slot) {
            slot_ = slot;
            pending_commit_ = true;
        }
    }
    IoSlot slot() const noexcept { return slot_; }
protected:
    PollSlot ctl_{};
    bool pending_commit_ {false};
};

}
}