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

#include <toolbox/sys/Log.hpp>

#ifndef TOOLBOX_IO_HOOK_HPP
#define TOOLBOX_IO_HOOK_HPP

#include <toolbox/sys/Time.hpp>
#include <toolbox/util/Slot.hpp>

#include <boost/intrusive/list.hpp>

namespace toolbox {
inline namespace io {

struct Hook
: boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
    using Slot = BasicSlot<CyclTime>;
    Hook(Slot slot) noexcept
    : slot{slot}
    {
    }
    Hook() = default;
    Slot slot;
};

using HookList = boost::intrusive::list<Hook, boost::intrusive::constant_time_size<false>>;

//TOOLBOX_API void dispatch(CyclTime now, const HookList& l) noexcept;

inline void dispatch(CyclTime now, const HookList& l) noexcept
{
    auto it = l.begin();
    while (it != l.end()) {
        // Increment iterator before calling each hook, so that hooks can safely unhook themselves.
        const auto prev = it++;
        try {
            prev->slot(now);
        } catch (const std::exception& e) {
            TOOLBOX_ERROR << "error handling hook: " << e.what();
        }
    }
}

} // namespace io
} // namespace toolbox

#endif // TOOLBOX_IO_HOOK_HPP
