#pragma once

#include <ostream>
#include <toolbox/util/Enum.hpp>

namespace toolbox {
inline namespace io {

enum State: std::uint16_t {
    Starting,
    Started,
    Stopping,
    Stopped,
    Failed,
    Crashed
};

inline std::ostream& operator<<(std::ostream& os, const State& self) {
    switch(self) {
        case State::Starting: return os << "Starting";
        case State::Started: return os << "Started";
        case State::Stopping: return os << "Stopping";
        case State::Stopped: return os << "Stopped";
        case State::Failed: return os << "Failed";
        default: return os << unbox(self);
    }
}

} // ns io
} // ns toolbox