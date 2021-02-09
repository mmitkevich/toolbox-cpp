#pragma once

#include <atomic>
#include <cstdint>
#include "toolbox/sys/Error.hpp"
#include <toolbox/io/Hook.hpp>
#include <toolbox/io/Waker.hpp>
#include <toolbox/io/Timer.hpp>
#include <toolbox/io/State.hpp>
#include <toolbox/io/Handle.hpp>
#include <thread>

namespace toolbox {
inline namespace io {

constexpr Duration NoTimeout{-1};
enum class Priority { High = 0, Low = 1 };

class IPoller;
class Reactor;

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

using IoSlot = BasicSlot<CyclTime, int, PollEvents>;

// FDS or ptr
class PollFD {
public:
    using SID = int;

    struct FDS {
        int fd;     // linux file handle
        int sid;
    };

    explicit PollFD(int fd) {
        fd_ = fd;
        sid_ = 0;
    }

    explicit PollFD(void* ptr) {
        ptr_ = ptr;
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
        std::swap(fd_, rhs.fd_);
        std::swap(sid_, rhs.sid_);
        std::swap(ptr_, rhs.ptr_);
        std::swap(events_, rhs.events_);
        std::swap(slot_, rhs.slot_);
    }

    void reset() {
        fd_ = -1;
        sid_ = 0;
        ptr_ = nullptr;
        events_ = PollEvents::None;
        slot_.reset();
    }

    int fd() const noexcept { return fd_; }
    void fd(int fd) noexcept { fd_ = fd; }

    int sid() const noexcept { return sid_; }
    void sid(int sid) noexcept { sid_ = sid; }

    int next_sid() noexcept { return ++sid_;}
    
    void* ptr() const noexcept { return ptr_; }

    bool empty() const noexcept { return slot_.empty(); }
    explicit operator bool() const noexcept { return !empty(); }

    PollEvents events() const noexcept { return events_; }
    void events(PollEvents events) noexcept { events_ = events; }
    
    IoSlot slot() const noexcept { return slot_; }
    void slot(IoSlot slot) noexcept { slot_ = slot; }
protected:
    int fd_{};     // index of poll data structure in the poller
    int sid_{};
    void *ptr_{};  // type erased client object (say, custom queue) to poll
    PollEvents events_ {PollEvents::None};
    IoSlot slot_ {};
};

class PollHandle;

class IRunnable {
    virtual void run() = 0;
};

/// subscribe/unsubscribe
class IPoller {
public:
    /// update subscription. TODO: refactor into subscribe/unsubscribe
    virtual bool ctl(PollHandle& handle) = 0;
};

class IReactor : virtual public IWaker, virtual public IRunnable {
public:
    virtual ~IReactor() = default;
    /// get implementation from fd
    virtual IPoller* poller(int fd) = 0;
    PollHandle handle(int fd);
};

// event subscription
class PollHandle : public PollFD  {
public:
    enum PollFlags: std::int32_t {
        BatchCtl   = 1,
        PendingCtl = 2
    };
    
    explicit PollHandle(int fd, IPoller* poller=nullptr)
    : PollFD(fd)
    , poller_(poller)
    {}

    explicit PollHandle(IPoller* poller = nullptr)
    : poller_(poller)
    {}
    
    ~PollHandle() { 
        reset();
    }

    // Copy.
    PollHandle(const PollHandle&) = delete;
    PollHandle& operator=(const PollHandle&) = delete;

    // Move.
    PollHandle(PollHandle&& rhs) noexcept {
        swap(rhs);
    }

    PollHandle& operator=(PollHandle&& rhs) noexcept {
        reset();
        swap(rhs);
        return *this;
    }

    void swap(PollHandle& rhs) noexcept {
        PollFD::swap(static_cast<PollFD&>(rhs));
        std::swap(poller_, rhs.poller_);
    }

    /// closes subscription
    void reset(std::nullptr_t=nullptr) noexcept {
        if(slot_!=nullptr || events_!=PollEvents::None) {
            events_ = PollEvents::None;
            slot_ = nullptr;
            commit();
        }
        PollFD::reset();
    }

    // remove events, keep slot
    void del(PollEvents events) noexcept {
        if(events_ & events) {
            events_ = events_ - events;
            commit();
        }
    }

    /// remove events, change slot
    void del(PollEvents events, IoSlot slot) noexcept {
        if((events_ & events) || (slot != slot_)) {
            events_ = events_ - events;
            slot_ = slot;
            commit();
        }
    }

    void commit(bool force=false) noexcept {
        // possible optimize more with constexpr if (epoll.is_et())....
        if(force || !(flags_ & PollFlags::BatchCtl) || (flags_ & PollFlags::PendingCtl) ) {
            //TOOLBOX_DEBUG<<"poll_commit "<<events_;
            poller_->ctl(*this);
            flags_ &= ~PollFlags::PendingCtl;
        }
    }

    bool batching(bool enable) {
        bool prev = flags_ & PollFlags::BatchCtl;
        if(enable)
            flags_ |= PollFlags::BatchCtl;
        else
            flags_ &= ~PollFlags::BatchCtl;
        return prev;
    }

    /// adds events, keep slot
    void add(PollEvents events) noexcept {
        if((events_ & events) != events) {
            events_ = events_ + events;
            if(slot_)
                commit();
        }
    }
    /// adds events, change slot
    void add(PollEvents events, IoSlot slot) noexcept {
        if(slot!=slot_ || (events_ & events) != events) {
            slot_ = slot;
            events_ = events_ + events;
            commit();
        }
    }

    /// change slot
    void mod(IoSlot slot) noexcept {
        if(slot_!=slot) {
            slot_ = slot;
            commit();
        }
    }
    IoSlot slot() const noexcept { return slot_; }
protected:
    IPoller* poller_{};
    std::int32_t flags_ {};
};


inline PollHandle IReactor::handle(int fd) {
    return PollHandle(fd, poller(fd));
}


/// basic reactor (implements timers)
class Reactor : virtual public IReactor {
    using Self = Reactor;
public:
    Reactor() = default;

    // Copy.
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    // Move.
    Reactor(Reactor&&) = delete;
    Reactor& operator=(Reactor&&) = delete;


    /// Throws std::bad_alloc only.
    [[nodiscard]] 
    Timer timer(MonoTime expiry, Duration interval, Priority priority, TimerSlot slot) {
        return timers(priority).insert(expiry, interval, slot);
    }
    /// Throws std::bad_alloc only.
    [[nodiscard]] 
    Timer timer(MonoTime expiry, Priority priority, TimerSlot slot) {
        return timers(priority).insert(expiry, slot);
    }
    
    void stop() {
      if(state()!=State::PendingClosed && state()!=State::Closed) {
          state(State::Closed);
          stop_.store(true, std::memory_order_release);
      }
    }

    // clang-format on
    void add_hook(Hook& hook) noexcept { hooks_.push_back(hook); }
    HookList& hooks() noexcept { return hooks_; }

    void run() override
    {
        state(State::PendingOpen);
        state(State::Open);
        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
            auto now = CyclTime::now();
            if(0==timers(Priority::High).dispatch(now)) {
                timers(Priority::Low).dispatch(now);
            }
        }
        state(State::PendingClosed);
        state(State::Closed);
    }
    
    MonoTime next_expiry(MonoTime next) const {
        using namespace std::chrono;
        {
            auto& tq = timers(Priority::High);
            if (!tq.empty()) {
                // Duration until next expiry. Mitigate scheduler latency by preempting the
                // high-priority timer and busy-waiting for 200us ahead of timer expiry.
                next = min(next, tq.front().expiry() - 200us);
            }
        }
        {
            auto& tq = timers(Priority::Low);
            if (!tq.empty()) {
                // Duration until next expiry.
                next = min(next, tq.front().expiry());
            }
        }
        return next;
    }

    const TimerQueue& timers(Priority priority) const {
        return tqs_[static_cast<int>(priority)];
    }

    TimerQueue& timers(Priority priority) {
        return tqs_[static_cast<int>(priority)];
    }

    auto& state_changed() noexcept { return state_changed_; }
    State state() const noexcept { return state_; }
    void state(State val) noexcept { 
        state_.store(val, std::memory_order_release); 
        state_changed().invoke(this, val); 
    }

    void wakeup() noexcept override {}
protected:
    static_assert(static_cast<int>(Priority::High) == 0);
    static_assert(static_cast<int>(Priority::Low) == 1);
    std::atomic<bool> stop_{false};
    Signal<Reactor*, State> state_changed_;
    std::atomic<State> state_{State::Closed};
    TimerPool tp_;
    std::array<TimerQueue, 2> tqs_{tp_, tp_};
    HookList hooks_;    
};

}// io
}// toolbox