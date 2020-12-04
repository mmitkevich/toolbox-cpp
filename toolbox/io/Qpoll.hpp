#pragma once

#include "toolbox/io/PollHandle.hpp"
#include <toolbox/ipc/MagicRingBuffer.hpp>

namespace toolbox {
inline namespace io {

template<typename QueueT> 
class BasicQueuePoll {
public:
    bool open(PollHandle& handle) {
        auto ix = static_cast<std::size_t>(handle.fd());
        if(ix>=data_.size())
            data_.resize(ix+1);
        data_[ix] = handle;
        return true;
    }
    void close(PollHandle& handle) {
        auto ix = static_cast<std::size_t>(handle.fd());
        assert(ix<data_.size());
        data_[ix].reset();
    }
    int wait(std::error_code& ec) noexcept {
        int n = 0;
        for(auto& ref: data_) {
            auto &q = *static_cast<QueueT*>(ref.ptr());
            if(q) {
                auto ev = ref.events();
                if(q.size()>0) {
                    ev = ev + PollEvents::Read;
                }
                if(q.available()>0) {
                    ev = ev + PollEvents::Write;
                }
                ref.events(ev);
                if(ev != PollEvents::None)
                    n++;
            }
        }
        return n;
    }
    int wait(MonoTime timeout, std::error_code& ec) noexcept {
        return wait(ec);
    }
    int dispatch(CyclTime now) {
        return 0;   
    }
private:
    std::vector<PollFD> data_;

};

using Qpoll = BasicQueuePoll<ipc::MagicRingBuffer>;
}
}