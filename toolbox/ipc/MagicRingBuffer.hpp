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

#pragma once

#include "toolbox/io/Handle.hpp"
#include <cstring>
#include <string_view>
#include <toolbox/io/File.hpp>
#include <toolbox/ipc/Mmap.hpp>
#include <toolbox/sys/Limits.hpp>

#include <atomic>
#include <cassert>
#include <memory>
#include <unistd.h>

#include <iostream>

namespace toolbox {
inline namespace ipc {


/// MagicRingBuffer is a SPSC queue suitable for variable length messages
class MagicRingBuffer {
  public:
    struct /*alignas(CacheLineSize) -- FIXME clang*/ Impl {
        std::atomic<std::int64_t> rpos;
        alignas(CacheLineSize) std::atomic<std::int64_t> wpos;
        alignas(PageSize) char buf[];
    };
    static_assert(std::is_trivially_copyable_v<Impl>);
    static_assert(sizeof(Impl) == PageSize);
    static_assert(offsetof(Impl, rpos) == 0 * CacheLineSize);
    static_assert(offsetof(Impl, wpos) == 1 * CacheLineSize);
    static_assert(offsetof(Impl, buf) == PageSize);

    //constexpr MagicRingBuffer(std::nullptr_t = nullptr) noexcept {}
    

    explicit MagicRingBuffer(std::size_t capacity)
    : capacity_{capacity}
    , mask_{mask(capacity_)}
    , allocator_{MmapFlags::Magic, sizeof(Impl)}
    , impl_{reinterpret_cast<Impl*>(allocator_.allocate(sizeof(Impl)+capacity_))}
    {
        std::memset(reinterpret_cast<void*>(impl_), 0, sizeof(Impl)+capacity_);
    }
    explicit MagicRingBuffer(FileHandle& fh)
    : capacity_{capacity(io::file_size(fh.get()))}
    , mask_{mask(capacity_)}
    , allocator_{MmapFlags::Magic|MmapFlags::Shared, sizeof(Impl)}
    , impl_{reinterpret_cast<Impl*>(allocator_.allocate(fh, sizeof(Impl)+capacity_))}
    {
    }
    explicit MagicRingBuffer(FileHandle&& fh)
    : MagicRingBuffer{fh}
    {
    }
    /// Opens a file-backed MpmcQueue.
    ///
    /// The mmap() function retains a reference to the file associated with the file descriptor,
    /// so the file can be safely closed once the mapping has been established.
    ///
    /// \param path Path to MpmcQueue file.
    ///
    explicit MagicRingBuffer(const char* path)
    : MagicRingBuffer{os::open(path, O_RDWR)}
    {
    }
    ~MagicRingBuffer() {
        allocator_.deallocate(reinterpret_cast<char*>(impl_), sizeof(Impl)+capacity_);
    }

    // Copy.
    MagicRingBuffer(const MagicRingBuffer&) = delete;
    MagicRingBuffer& operator=(const MagicRingBuffer&) = delete;

    // Move.
    MagicRingBuffer(MagicRingBuffer&& rhs) noexcept
    : capacity_{rhs.capacity_}
    , mask_{rhs.mask_}
    , allocator_{std::move(rhs.allocator_)}
    , impl_{rhs.impl_}
    {
        rhs.capacity_ = 0;
        rhs.mask_ = 0;
        rhs.impl_ = nullptr;
    }
    MagicRingBuffer& operator=(MagicRingBuffer&& rhs) noexcept
    {
        reset();
        swap(rhs);
        return *this;
    }

    /// Returns true if the queue is empty.
    bool empty() const noexcept { return size() == 0; }
    /// Returns true if the queue is full.
    bool full() const noexcept { return size() == capacity_; }
    /// Returns the number of available elements.
    std::size_t available() const noexcept { return capacity_ - size(); }
    /// Returns the maximum number of elements the queue can hold.
    /// I.e. the queue's capacity.
    std::size_t capacity() const noexcept { return capacity_; }
    /// Returns the maximum number of elements the queue can hold.
    /// I.e. the queue's capacity.
    std::size_t max_size() const noexcept { return capacity(); }
    /// Returns true if the number of elements in the queue.
    std::size_t size() const noexcept
    {
        // Acquire prevents reordering of these loads.
        const auto rpos = impl_->rpos.load(std::memory_order_acquire);
        const auto wpos = impl_->wpos.load(std::memory_order_relaxed);
        return wpos - rpos;
    }
    void reset(std::nullptr_t = nullptr) noexcept
    {
        if(impl_!=nullptr)
            allocator_.deallocate(reinterpret_cast<char*>(impl_), sizeof(Impl)+capacity_);
        // Reverse order.
        impl_ = nullptr;
        mask_ = 0;
        capacity_ = 0;
    }
    void swap(MagicRingBuffer& rhs) noexcept
    {
        std::swap(capacity_, rhs.capacity_);
        std::swap(mask_, rhs.mask_);
        std::swap(impl_, rhs.impl_);
        std::swap(allocator_, rhs.allocator_);
    }
    /// Read at least size bytes or return false
    template <typename FnT>
    bool read(std::size_t size, FnT fn) noexcept
    {
        static_assert(std::is_nothrow_invocable_v<FnT, const char*, std::size_t>);
        auto rpos = impl_->rpos.load(std::memory_order_acquire);
        auto wpos = impl_->wpos.load(std::memory_order_relaxed);
        std::size_t ready = (std::size_t)(wpos-rpos);
        if(ready==0 || ready<size)
            return false;
        std::size_t consumed = fn(impl_->buf+(rpos&mask_), ready);  
        impl_->rpos.store(rpos + consumed, std::memory_order_release);
        return true;
    }
    /// Read available data up to size, return number of bytes read
    std::size_t read(void* buf, std::size_t size) noexcept {
        return read(0, [buf, size](const char* data, std::size_t len) noexcept {
            std::size_t rlen = std::min(size, len);
            std::memcpy(buf, data, rlen);
            return rlen;
        });
    }
    template<typename T>
    bool read(T& val) noexcept {
        return read(sizeof(T), [&val](const char*buf, std::size_t size) noexcept {
            val = *reinterpret_cast<const T*>(buf);
            return sizeof(T);
        });
    }
    /// Returns false if capacity is exceeded.
    template <typename FnT>
    bool write(std::size_t size, FnT fn) noexcept
    {
        auto wpos = impl_->wpos.load(std::memory_order_acquire);
        auto rpos = impl_->rpos.load(std::memory_order_relaxed);
        if(capacity_-(wpos-rpos)<size)
            return false;
        fn(impl_->buf+(wpos&mask_), size);
        impl_->wpos.store(wpos+size, std::memory_order_release);
        return true;
    }
    
    std::size_t write(const void* buf, std::size_t size) {
        return write(size, [&](void* ptr, std::size_t len) {
            std::memcpy(ptr, buf, len);
        }) ? size : 0;
    }

    template<typename T>
    bool write(const T& val) {
        return write(&val, sizeof(val));
    }

    bool write(std::string_view sv) {
        return write(sv.data(), sv.size());
    }

    bool write(const std::string& s) {
        return write(s.data(), s.size());
    }

  private:
    static constexpr std::size_t capacity(std::size_t size) noexcept
    {
        return (size - sizeof(Impl));
    }
    static constexpr std::size_t size(std::size_t capacity) noexcept
    {
        return sizeof(Impl) + capacity;
    }
    static constexpr std::size_t mask(std::size_t capacity) {
        if (!is_pow2(capacity)) {
            throw std::runtime_error{"capacity not a power of two"};
        }
        return capacity - 1;
    }
    std::uint64_t capacity_{}, mask_{};
    MmapAllocator<char> allocator_;
    Impl* impl_{nullptr};
};

} // namespace ipc
} // namespace toolbox


