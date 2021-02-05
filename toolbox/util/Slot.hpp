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

#include "toolbox/util/RobinHood.hpp"
#ifndef TOOLBOX_UTIL_SLOT_HPP
#define TOOLBOX_UTIL_SLOT_HPP

#include <toolbox/Config.h>

#include <boost/container/small_vector.hpp>
#include <toolbox/util/Traits.hpp>
#include <system_error>
#include <utility>
#include <algorithm>

namespace toolbox {
inline namespace util {

struct SlotData {
    void* obj {nullptr};
    void (*fn)(...) {nullptr};
};

template <typename... ArgsT>
class BasicSlot {
  public:
    friend constexpr bool operator==(BasicSlot lhs, BasicSlot rhs) noexcept
    {
        return lhs.obj_ == rhs.obj_ && lhs.fn_ == rhs.fn_;
    }
    friend constexpr bool operator!=(BasicSlot lhs, BasicSlot rhs) noexcept
    {
        return !(lhs == rhs);
    }
    constexpr BasicSlot(std::nullptr_t = nullptr) noexcept {}
    ~BasicSlot() = default;

    BasicSlot(const SlotData& rhs) noexcept {
        obj_ = rhs.obj;
        fn_ = reinterpret_cast<void (*)(void*, ArgsT...)>(rhs.fn);
    }
    
    operator SlotData&() {
        return *reinterpret_cast<SlotData*>(this);
    }

    // Copy.
    constexpr BasicSlot(const BasicSlot&) noexcept = default;
    constexpr BasicSlot& operator=(const BasicSlot&) noexcept = default;

    // Move.
    constexpr BasicSlot(BasicSlot&&) noexcept = default;
    constexpr BasicSlot& operator=(BasicSlot&&) noexcept = default;

    /// Unsafe invoke
    TOOLBOX_ALWAYS_INLINE void invoke(ArgsT... args) const {
        fn_(obj_, std::forward<ArgsT>(args)...); 
    }
    /// Safe call
    TOOLBOX_ALWAYS_INLINE void operator()(ArgsT... args) const {
        if(fn_!=nullptr)
            fn_(obj_, std::forward<ArgsT>(args)...); 
    }
    constexpr bool empty() const noexcept { return fn_ == nullptr; }
    constexpr explicit operator bool() const noexcept { return fn_ != nullptr; }

    // Free function.
    template <void (*FnT)(ArgsT...)>
    constexpr auto& bind() noexcept
    {
        obj_ = nullptr;
        fn_ = [](void* obj, ArgsT... args) { FnT(std::forward<ArgsT>(args)...); };
        return *this;
    }
    // Lambda function (ref).
    template <typename ClassT>
    constexpr auto& bind(ClassT* obj) noexcept
    {
        obj_ = obj;
        fn_ = [](void* obj, ArgsT... args) {
            (*static_cast<ClassT*>(obj))(std::forward<ArgsT>(args)...);
        };
        return *this;
    }
    // Lambda function (copy).
    template <typename LambdaFnT>
    constexpr auto& bind(LambdaFnT&& fn) noexcept
    {
        //https://stackoverflow.com/questions/37481767/why-does-a-lambda-have-a-size-of-1-byte
        static_assert(sizeof(LambdaFnT) <= sizeof(void*), "Can only bind to lambda with captures less then 8 bytes"); 
        obj_ = *reinterpret_cast<void**>(&fn);

        fn_ = [](void* obj, ArgsT... args) {
            void (LambdaFnT::*pmf)(ArgsT...) const = &LambdaFnT::operator();
            char* pch = reinterpret_cast<char*>(&obj);
            LambdaFnT* ptr = reinterpret_cast<LambdaFnT*>(pch);
            (ptr->*pmf)(std::forward<ArgsT>(args)...);
        };
        return *this;
    }

    BasicSlot<ArgsT...> release() {
        auto result = *this;
        reset(nullptr);
        return result;
    }
    // Member function.
    template <auto MemFnT, typename ClassT = typename FunctionTraits<decltype(MemFnT)>::ClassType>
    constexpr auto& bind(ClassT* obj) noexcept
    {
        obj_ = obj;
        fn_ = [](void* obj, ArgsT... args) {
            (static_cast<ClassT*>(obj)->*MemFnT)(std::forward<ArgsT>(args)...);
        };
        return *this;
    }
    void reset(std::nullptr_t = nullptr) noexcept
    {
        obj_ = nullptr;
        fn_ = nullptr;
    }

    bool connect(BasicSlot rhs) noexcept {
        *this = rhs;
        return true;
    }

    bool disconnect(BasicSlot rhs) noexcept {
        reset();
        return true;
    }

  private:
    void* obj_{nullptr};
    void (*fn_)(void*, ArgsT...){nullptr};
};

template<typename...ArgsT>
using Slot = BasicSlot<ArgsT...>;

template <auto FnT>
constexpr auto bind() noexcept
{
    using Traits = FunctionTraits<decltype(FnT)>;
    using Slot = typename Traits::template Pack<BasicSlot>;
    return Slot{}.template bind<FnT>();
}

template <typename LambdaFnT>
constexpr auto bind( LambdaFnT&& fn) noexcept
{
    using Traits = FunctionTraits<LambdaFnT>;
    using Slot = typename Traits::template Pack<BasicSlot>;
    return Slot{}.template bind<LambdaFnT>(std::move(fn));
}

template <typename ClassT>
constexpr auto bind(ClassT* obj) noexcept
{
    using Traits = FunctionTraits<decltype(&ClassT::operator())>;
    using Slot = typename Traits::template Pack<BasicSlot>;
    return Slot{}.bind(obj);
}

template <auto MemFnT, typename ClassT = typename FunctionTraits<decltype(MemFnT)>::ClassType>
constexpr auto bind(ClassT* obj) noexcept
{
    using Traits = FunctionTraits<decltype(MemFnT)>;
    using Slot = typename Traits::template Pack<BasicSlot>;
    return Slot{}.template bind<MemFnT>(obj);
}

template<std::size_t Capacity, typename...ArgsT>
class BasicSignal {
public:
    using Slot = BasicSlot<ArgsT...>;
    class Handle {
        BasicSignal &self;
        Slot slot;
    public:
        Handle(BasicSignal& self, Slot slot) noexcept
        : self(self), slot(slot) {
            self.connect(slot);
        }
        Handle(const Handle&) noexcept = delete;
        Handle(Handle&& rhs) noexcept = default;
        ~Handle() {
            self.disconnect(slot);
        }
    };
    
    Handle subscribe(Slot slot) {
        return Handle(*this, slot);
    }

    bool connect(Slot slot) {
        auto it = std::find(slots_.begin(), slots_.end(), slot);
        if(it==slots_.end()) {
            // FIXME: when full, do gc
            slots_.emplace_back(slot);
            return true;
        }
        return false;
    }
    bool disconnect(Slot slot) {
        auto it = std::find(slots_.begin(), slots_.end(), slot);
        if(it!=slots_.end()) {
            slots_.erase(it);
            return true;
        }
        return false;
    }
    void disconnect_all() {
        slots_.clear();
    }

    TOOLBOX_ALWAYS_INLINE void invoke(ArgsT... args) const { 
        for(auto& slot: slots_) {
            slot(std::forward<ArgsT>(args)...);
        }
    }

    TOOLBOX_ALWAYS_INLINE void operator()(ArgsT... args) const { 
        for(auto& slot: slots_) {
            slot(std::forward<ArgsT>(args)...);
        }
    }
    
    /// one-shot notify. removes slot after calling
    void notify(ArgsT... args) const {
        for(auto& slot: slots_) {
            slot(std::forward<ArgsT>(args)...);
            slot.reset();
        }
    }

    constexpr bool empty() const noexcept { return slots_.empty(); }
    constexpr explicit operator bool() const noexcept { return !slots_.empty(); }
private:
    boost::container::small_vector<Slot, Capacity> slots_;
};

template<typename...Args>
using Signal = BasicSignal<16, Args...>;

template<typename KeyT, typename...ArgsT>
class SignalMap {
public:
    using Slot = util::Slot<ArgsT...>;
    using Key = KeyT;
    using key_type = Key;
    using value_type = Slot;

    bool empty(Key key) {
        return data_.find(key)==data_.end();
    }

    bool emplace(Key key, Slot slot) {
        auto it = data_.emplace(key, slot);
        return it.second;
    }

    bool erase(Key key) {
        auto it = data_.find(key);
        if(it!=data_.end()) {
            data_.erase(it);
            return true;
        }
        return false;
    }

    void operator()(Key key, ArgsT... args) {
        auto it = data_.find(key);
        if(it!=data_.end()) {
            it->second(std::forward<ArgsT...>(args)...);
        }
    }
    
    void invoke(Key key, ArgsT... args) {
        auto it = data_.find(key);
        if(it!=data_.end()) {
            it->second.invoke(std::forward<ArgsT...>(args)...);
        }
    }

    void notify(Key key, ArgsT... args) {
        auto it = data_.find(key);
        if(it!=data_.end()) {
            auto slot = it->second;
            data_.erase(it);
            slot.invoke(std::forward<ArgsT...>(args)...);
        }
    }

    Slot& operator[](Key key) {
        return data_[key];
    }
protected:
    util::RobinFlatMap<KeyT, util::Slot<ArgsT...>> data_;
};

using DoneSlot = Slot<std::error_code>;
using SizeSlot = Slot<ssize_t, std::error_code>;


} // namespace util
} // namespace toolbox

#endif // TOOLBOX_UTIL_SLOT_HPP
