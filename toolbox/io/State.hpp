#pragma once

#include <ostream>
#include <toolbox/util/Enum.hpp>

namespace toolbox {
inline namespace io {

enum State: std::uint16_t {
    Unknown,
    PendingOpen,
    Open,
    PendingClosed,
    Closed,
    Failed,
    Crashed
};

inline std::ostream& operator<<(std::ostream& os, const State& self) {
    switch(self) {
        case State::Unknown: return os <<"Unknown";
        case State::PendingOpen: return os << "PendingOpen";
        case State::Open: return os << "Open";
        case State::PendingClosed: return os << "PendingClosed";
        case State::Closed: return os << "Closed";
        case State::Failed: return os << "Failed";
        case State::Crashed: return os << "Crashed";
        default: return os << unbox(self);
    }
}

} // ns io
} // ns toolbox