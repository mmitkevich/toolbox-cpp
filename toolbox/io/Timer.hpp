// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
// Copyright (C) 2020 Reactive Markets Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TOOLBOX_IO_TIMER_HPP
#define TOOLBOX_IO_TIMER_HPP

#include <toolbox/sys/Time.hpp>
#include <toolbox/util/Slot.hpp>

#include <toolbox/Config.h>

#include <boost/intrusive_ptr.hpp>

#include <memory>
#include <vector>

namespace toolbox {
inline namespace io {

class Timer;
class TimerQueue;
using TimerSlot = BasicSlot<CyclTime, Timer&>;

class TOOLBOX_API Timer {
    friend class TimerQueue;

  public:
    struct Impl {
        union {
            /// Singly-linked free-list.
            Impl* next;
            TimerQueue* tq;
        };
        int ref_count;
        long id;
        MonoTime expiry;
        Duration interval;
        TimerSlot slot;
    };

    explicit Timer(Impl* impl)
    : impl_{impl, false}
    {
    }
    Timer(std::nullptr_t = nullptr) noexcept {}
    ~Timer() = default;

    // Copy.
    Timer(const Timer&) = default;
    Timer& operator=(const Timer&) = default;

    // Move.
    Timer(Timer&&) noexcept = default;
    Timer& operator=(Timer&&) noexcept = default;

    bool empty() const noexcept { return !impl_; }
    explicit operator bool() const noexcept { return impl_ != nullptr; }
    long id() const noexcept { return impl_ ? impl_->id : 0; }
    bool pending() const noexcept { return impl_ != nullptr && bool{impl_->slot}; }

    MonoTime expiry() const noexcept { return impl_->expiry; }
    Duration interval() const noexcept { return impl_->interval; }
    /// Setting the interval will not reschedule any pending timer.
    template <typename RepT, typename PeriodT>
    void set_interval(std::chrono::duration<RepT, PeriodT> interval) noexcept
    {
        using namespace std::chrono;
        impl_->interval = duration_cast<Duration>(interval);
    }
    void reset(std::nullptr_t = nullptr) noexcept { impl_.reset(); }
    void swap(Timer& rhs) noexcept { impl_.swap(rhs.impl_); }
    void cancel() noexcept;

  private:
    void set_expiry(MonoTime expiry) noexcept { impl_->expiry = expiry; }
    TimerSlot& slot() noexcept { return impl_->slot; }

    boost::intrusive_ptr<Timer::Impl> impl_;
};

/// Equal to.
inline bool operator==(const Timer& lhs, const Timer& rhs) noexcept
{
    return lhs.id() == rhs.id();
}

/// Not equal to.
inline bool operator!=(const Timer& lhs, const Timer& rhs) noexcept
{
    return lhs.id() != rhs.id();
}

/// Less than.
inline bool operator<(const Timer& lhs, const Timer& rhs) noexcept
{
    return lhs.id() < rhs.id();
}

/// Greater than.
inline bool operator>(const Timer& lhs, const Timer& rhs) noexcept
{
    return lhs.id() > rhs.id();
}

/// Less than or equal to.
inline bool operator<=(const Timer& lhs, const Timer& rhs) noexcept
{
    return lhs.id() <= rhs.id();
}

/// Greater than or equal to.
inline bool operator>=(const Timer& lhs, const Timer& rhs) noexcept
{
    return lhs.id() >= rhs.id();
}

class TOOLBOX_API TimerPool {
    using SlabPtr = std::unique_ptr<Timer::Impl[]>;

  public:
    TimerPool() = default;

    // Copy.
    TimerPool(const TimerPool&) = delete;
    TimerPool& operator=(const TimerPool&) = delete;

    // Move.
    TimerPool(TimerPool&&) = delete;
    TimerPool& operator=(TimerPool&&) = delete;

    Timer::Impl* alloc(MonoTime expiry, Duration interval, TimerSlot slot);
    void dealloc(Timer::Impl* impl) noexcept
    {
        assert(impl);
        impl->next = free_;
        free_ = impl;
    }

  private:
    std::vector<SlabPtr> slabs_;
    /// Head of free-list.
    Timer::Impl* free_{nullptr};
    /// Heap of timers ordered by expiry time.
    std::vector<Timer> heap_;
};

class TOOLBOX_API TimerQueue {
    friend class Timer;
    friend void intrusive_ptr_add_ref(Timer::Impl*) noexcept;
    friend void intrusive_ptr_release(Timer::Impl*) noexcept;

    using SlabPtr = std::unique_ptr<Timer::Impl[]>;

  public:
    /// Implicit conversion from pool is allowed, so that TimerQueue arrays can be aggregate
    /// initialised.
    TimerQueue(TimerPool& pool)
    : pool_{pool}
    {
    }

    // Copy.
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    // Move.
    TimerQueue(TimerQueue&&) = delete;
    TimerQueue& operator=(TimerQueue&&) = delete;

    std::size_t size() const noexcept { return heap_.size() - cancelled_; }
    bool empty() const noexcept { return size() == 0; }
    const Timer& front() const { return heap_.front(); }

    // clang-format off
    /// Throws std::bad_alloc only.
    [[nodiscard]] Timer insert(MonoTime expiry, Duration interval, TimerSlot slot);
    /// Throws std::bad_alloc only.
    [[nodiscard]] Timer insert(MonoTime expiry, TimerSlot slot)
    {
        return insert(expiry, Duration::zero(), slot);
    }
    // clang-format on

    int dispatch(CyclTime now)
    {
        int work{};
        while (!heap_.empty()) {

            // If not pending, then must have been cancelled.
            if (!heap_.front().pending()) {
                pop();
                --cancelled_;
                assert(cancelled_ >= 0);
            } else if (heap_.front().expiry() <= now.mono_time()) {
                expire(now);
                ++work;
            } else {
                break;
            }
        }
        gc();
        return work;
    }

  private:
    Timer alloc(MonoTime expiry, Duration interval, TimerSlot slot);
    void cancel() noexcept;
    void expire(CyclTime now);
    void gc() noexcept
    {
        // Garbage collect if more than half of the timers have been cancelled.
        if (cancelled_ > static_cast<int>(heap_.size() >> 1)) {
            const auto it
                = remove_if(heap_.begin(), heap_.end(), [](const auto& tmr) { return !tmr.pending(); });
            heap_.erase(it, heap_.end());
            make_heap(heap_.begin(), heap_.end(), is_after);
            cancelled_ = 0;
        }
    }

    Timer pop() noexcept
    {
        auto tmr = heap_.front();
        pop_heap(heap_.begin(), heap_.end(), is_after);
        heap_.pop_back();
        return tmr;
    }

    static bool is_after(const Timer& lhs, const Timer& rhs)
    {
        return lhs.expiry() > rhs.expiry();
    }

    TimerPool& pool_;
    long max_id_{};
    int cancelled_{};
    /// Heap of timers ordered by expiry time.
    std::vector<Timer> heap_;
};

inline void intrusive_ptr_add_ref(Timer::Impl* impl) noexcept
{
    ++impl->ref_count;
}

TOOLBOX_API void intrusive_ptr_release(Timer::Impl* impl) noexcept;

inline void Timer::cancel() noexcept
{
    // If pending, then reset the slot and inform the queue that the timer has been cancelled.
    if (impl_->slot) {
        impl_->slot.reset();
        impl_->tq->cancel();
    }
}
} // namespace io
} // namespace toolbox

#endif // TOOLBOX_IO_TIMER_HPP
