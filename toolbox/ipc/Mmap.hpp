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

#ifndef TOOLBOX_IPC_MMAP_HPP
#define TOOLBOX_IPC_MMAP_HPP

#include "toolbox/io/Handle.hpp"
#include "toolbox/sys/Limits.hpp"
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <toolbox/sys/Error.hpp>
#include <toolbox/io/File.hpp>
#include <toolbox/Config.h>
#include <toolbox/util/Enum.hpp>

#include <memory>
#include <system_error>

#include <sys/mman.h>

namespace toolbox {
inline namespace ipc {

/// Memory-mapped addressed may be zero (in rare situations), but can never be MAP_FAILED.
class MmapPointer {
  public:
    MmapPointer(std::nullptr_t = nullptr) noexcept {}
    MmapPointer(void* ptr, std::size_t size) noexcept
    : ptr_{ptr}
    , size_{size}
    {
    }
    void* get() const noexcept { return ptr_; }
    void* data() const noexcept { return ptr_; }
    std::size_t size() const noexcept { return size_; }
    explicit operator bool() const noexcept { return ptr_ != MAP_FAILED; }

  private:
    void* ptr_{MAP_FAILED};
    std::size_t size_{0};
};

inline bool operator==(MmapPointer lhs, MmapPointer rhs)
{
    return lhs.get() == rhs.get() && lhs.size() == rhs.size();
}

inline bool operator!=(MmapPointer lhs, MmapPointer rhs)
{
    return !(lhs == rhs);
}

struct MmapDeleter {
    using pointer = MmapPointer;
    void operator()(MmapPointer p) const noexcept
    {
        if (p) {
            munmap(p.get(), p.size());
        }
    }
};

using Mmap = std::unique_ptr<MmapPointer, MmapDeleter>;

static constexpr std::size_t round_up_page_size(std::size_t n) { return (n+(toolbox::sys::PageSize>>1)) & ~(PageSize-1); };

enum MmapFlags : int {
    Magic    = 1,
    Shared   = 2,
    Readonly = 4
};

template <class T>
class MmapAllocator
{
public:
    using value_type    = T;

    constexpr MmapAllocator(int flags = 0, std::size_t header_size=0, std::string_view path="/dev/shm/tbrb-XXXXXX") noexcept
    : flags_(flags)
    , hlen_(header_size > 0 ? round_up_page_size(header_size):0)
    , path_(path) 
    {}

    template <class U> MmapAllocator(MmapAllocator<U> const&) noexcept {}

    value_type* allocate(std::size_t n)
    {
        FileHandle fd;
        return allocate(fd, n);
    }

    value_type* allocate(FileHandle& fd, std::size_t n)
    {
        std::size_t len = sizeof(value_type) * n;
        std::size_t plen = round_up_page_size(len);
        std::size_t tlen = (flags_&unbox(MmapFlags::Magic)) ? ((plen-hlen_)<<1)+hlen_ : plen;

        int prot = (flags_ & unbox(MmapFlags::Readonly)) ? PROT_READ: PROT_READ|PROT_WRITE;

        if(fd.empty() && (flags_ & (unbox(MmapFlags::Magic)|unbox(MmapFlags::Shared)))) {
            fd.reset(::mkstemp(path_.data()));
            if(fd.empty()) 
                throw std::system_error{make_sys_error(errno), "mkstemp"};
            if(0!=(errno = posix_fallocate(fd.get(), 0, tlen))) {
                throw std::system_error{make_sys_error(errno), "posix_fallocate"};
            }
            if(!(flags_&unbox(MmapFlags::Shared)))
                ::unlink(path_.c_str());
        }

        if(!(flags_ & unbox(MmapFlags::Magic))) {
            int flg = MAP_ANON;
            if(!(flags_ & unbox(MmapFlags::Shared)))
                flg |= MAP_PRIVATE;
            void* ptr = ::mmap(nullptr, len, prot, flg, fd.get(), 0);
            if(ptr==nullptr)
                throw std::system_error{make_sys_error(errno), "mmap"};
            return reinterpret_cast<value_type*>(ptr);
        } else {
            char* ptr = (char*) ::mmap(nullptr, tlen, prot, MAP_ANON|MAP_PRIVATE, -1, 0);
            if(ptr == nullptr)
                throw std::system_error{make_sys_error(errno), "mmap"};
            char* ptr1 = (char*) ::mmap(ptr + hlen_, plen-hlen_, prot, MAP_FIXED | MAP_SHARED, fd.get(), 0);
            if(ptr + hlen_ != ptr1) 
               throw std::system_error{make_sys_error(errno), "mmap"};
            char* ptr2 = (char*) ::mmap(ptr + plen, plen-hlen_, prot, MAP_FIXED | MAP_SHARED, fd.get(), 0);
            if(ptr + plen != ptr2) 
               throw std::system_error{make_sys_error(errno), "mmap"};
            return reinterpret_cast<value_type*>(ptr);
        }
    }
    void deallocate(value_type* p, std::size_t n) noexcept  // Use pointer if pointer is not a value_type*
    {
        std::size_t plen = round_up_page_size(n*sizeof(value_type));
        std::size_t mlen = plen;
        if(flags_&unbox(MmapFlags::Magic))
            mlen = ((plen-hlen_)<<1) + hlen_;
        if(0!=::munmap(p, mlen))
            std::terminate();
    }
    const std::string& path() const { return path_; }
    int flags() const { return flags_; }
    std::size_t header_size() { return hlen_; }
private:
    int flags_;
    std::size_t hlen_;
    std::string path_;
};

template <class T, class U>
bool operator==(MmapAllocator<T> const& lhs, MmapAllocator<U> const& rhs) noexcept { return true; }
template <class T, class U>
bool operator!=(MmapAllocator<T> const& lhs, MmapAllocator<U> const& rhs) noexcept { return !(lhs == rhs); }

} // namespace ipc
namespace os {

/// Map files or devices into memory.
inline Mmap mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off,
                 std::error_code& ec) noexcept
{
    const MmapPointer p{::mmap(addr, len, prot, flags, fd, off), len};
    if (!p) {
        ec = make_sys_error(errno);
    }
    return Mmap{p};
}

inline Mmap mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off)
{
    const MmapPointer p{::mmap(addr, len, prot, flags, fd, off), len};
    if (!p) {
        throw std::system_error{make_sys_error(errno), "mmap"};
    }
    return Mmap{p};
}

} // namespace os
} // namespace toolbox

#endif // TOOLBOX_IPC_MMAP_HPP
