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

#ifndef TOOLBOX_UTIL_ENUM_HPP
#define TOOLBOX_UTIL_ENUM_HPP

#include <cstdint>
#include <initializer_list>
#include <iosfwd>
#include <type_traits>

namespace toolbox {
inline namespace util {

template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
constexpr EnumT box(typename std::underlying_type_t<EnumT> val) noexcept
{
    return static_cast<EnumT>(val);
}

template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
constexpr typename std::underlying_type_t<EnumT> unbox(EnumT val) noexcept
{
    return static_cast<std::underlying_type_t<EnumT>>(val);
}

template<typename EnumT>
auto to_mask(EnumT val) noexcept  -> std::enable_if_t<std::is_enum_v<EnumT>, uint64_t>  {
    return 1ULL << unbox(val);
}

template<class EnumT>
class BitMask {
    class reference {
      public:
        constexpr reference(BitMask*parent, EnumT index)
        : parent_(parent)
        , index_(index) {}
        constexpr reference& operator=(bool val) {
            if(val)
                parent_->set(index_);
            else 
                parent_->reset(index_);
            return *this;
        }
        operator bool() const { return parent_->test(index_); }
      private:
        BitMask* parent_;
        EnumT index_;
    };
public:
    using value_type = EnumT;
    constexpr BitMask() = default;
    constexpr BitMask(std::initializer_list<EnumT> list) {
        value_ = 0;
        for(auto val: list) {
            value_ |= to_mask(val);
        }
    }
    constexpr operator bool() const noexcept { return value_!=0; }
    constexpr bool none() const noexcept { return value_!=0; }
    constexpr BitMask(uint64_t val) noexcept  : value_(val) {}
    constexpr bool test(EnumT val) const noexcept { return value_ &  to_mask(val); }
    constexpr BitMask<EnumT>& set(EnumT val) noexcept { value_ |= to_mask(val); return *this; }
    constexpr BitMask<EnumT>& set() noexcept { value_ = -1LL; return *this; }
    constexpr BitMask<EnumT>& reset(EnumT val) noexcept { value_ &= ~to_mask(val); return *this; }
    constexpr BitMask<EnumT>& reset() noexcept { value_ = 0; return *this; }
    constexpr reference operator[](EnumT val) const noexcept { return reference(this, val); }
    
    friend constexpr BitMask<EnumT> operator&(const BitMask<EnumT>&lhs, const BitMask<EnumT>&rhs) {
        return BitMask<EnumT>(lhs.value_ & rhs.value_);
    }
    friend constexpr BitMask<EnumT> operator|(const BitMask<EnumT>&lhs, const BitMask<EnumT>&rhs) {
        return BitMask<EnumT>(lhs.value_ | rhs.value_);
    }
    friend constexpr BitMask<EnumT> operator~(const BitMask<EnumT>&lhs) {
        return BitMask<EnumT>(~lhs.value_);
    }
private:
    uint64_t value_{0};
};


namespace operators {

template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
constexpr std::underlying_type_t<EnumT> operator & (std::underlying_type_t<EnumT> mask, EnumT val) noexcept
{
    return static_cast<EnumT>(mask & unbox(val));
}

template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
constexpr std::underlying_type_t<EnumT> operator | (EnumT mask, EnumT val) noexcept
{
    return static_cast<EnumT>(unbox(mask) | unbox(val));
}

template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
constexpr typename std::underlying_type_t<EnumT> operator |= (EnumT mask, EnumT val) noexcept
{
    return static_cast<EnumT>(unbox(val) | unbox(mask));
}

template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
constexpr EnumT operator ~(EnumT val) noexcept
{
    return static_cast<EnumT>(~static_cast<std::underlying_type_t<EnumT>>(val));
}

}

} // namespace util
} // namespace toolbox

#endif // TOOLBOX_UTIL_ENUM_HPP
