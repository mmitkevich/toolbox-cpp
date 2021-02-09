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

#ifndef TOOLBOX_IO_HANDLE_HPP
#define TOOLBOX_IO_HANDLE_HPP

#include <cstddef> // nullptr_t
#include <utility> // swap<>

#include <unistd.h> // close()
#include <toolbox/sys/Time.hpp>
#include <toolbox/util/Slot.hpp>

namespace toolbox {
inline namespace io {

template <typename PolicyT>
class BasicHandle {
  public:
    using value_type = int;

    static constexpr int invalid() noexcept { return PolicyT::invalid(); }

    constexpr BasicHandle(std::nullptr_t = nullptr) noexcept {}
    constexpr BasicHandle(int id) noexcept
    : id_{id}
    {
    }
    ~BasicHandle()
    {
        if (id_ != invalid()) {
            PolicyT::close(id_);
        }
    }

    // Copy.
    BasicHandle(const BasicHandle&) = delete;
    BasicHandle& operator=(const BasicHandle&) = delete;

    // Move.
    constexpr BasicHandle(BasicHandle&& rhs)
    : id_{rhs.id_}
    {
        rhs.id_ = invalid();
    }
    BasicHandle& operator=(BasicHandle&& rhs)
    {
        reset();
        swap(rhs);
        return *this;
    }

    constexpr bool empty() const noexcept { return id_ == invalid(); }
    constexpr explicit operator bool() const noexcept { return id_ != invalid(); }

    constexpr int get() const noexcept { return id_; }
    constexpr int operator*() const noexcept { return get(); }

    constexpr bool is_custom() const noexcept { return PolicyT::is_custom(id_); }

    int release() noexcept
    {
        const auto id = id_;
        id_ = invalid();
        return id;
    }
    void reset(std::nullptr_t p = nullptr) noexcept { reset(invalid()); }
    void reset(int id) noexcept
    {
        std::swap(id_, id);
        if (id != invalid()) {
            PolicyT::close(id);
        }
    }
    void swap(BasicHandle& rhs) noexcept { std::swap(id_, rhs.id_); }

  protected:
    int id_{invalid()};
};

template <typename PolicyT>
constexpr bool operator==(const BasicHandle<PolicyT>& lhs, const BasicHandle<PolicyT>& rhs)
{
    return lhs.get() == rhs.get();
}

template <typename PolicyT>
constexpr bool operator!=(const BasicHandle<PolicyT>& lhs, const BasicHandle<PolicyT>& rhs)
{
    return !(lhs == rhs);
}


struct FilePolicy {
    static constexpr bool is_custom(int fd) { return fd<0; }
    static constexpr int invalid() noexcept { return -1; }
    static void close(int fd) noexcept { ::close(fd); }
};

using FileHandle = BasicHandle<FilePolicy>;



} // namespace io
} // namespace toolbox

#endif // TOOLBOX_IO_HANDLE_HPP
