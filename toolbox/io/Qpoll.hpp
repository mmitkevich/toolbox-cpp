#pragma once
#include "toolbox/io/Poller.hpp"
#include <toolbox/ipc/MagicRingBuffer.hpp>

namespace toolbox {
inline namespace io {

template<typename QueueT> 
class BasicQueuePoll : public IPoller {
public:
    os::FD add(QueueT& queue, PollEvents events=PollEvents::None) {
        data_.emplace_back({&queue, events});
        return data_.size() - 1;
    }
    void del(os::FD fd) {
        std::size_t i = (std::size_t) fd;
        assert(i<data_.size());
        data_[i].queue = nullptr;
    }
    void mod(os::FD fd, PollEvents events) {
        std::size_t i = (std::size_t) fd;
        assert(i<data_.size());
        data_[i].events = events;
    }
    PollHandle subscribe(os::FD fd, PollEvents events, IoSlot slot) override {
        std::size_t i = (std::size_t) fd;
        assert(i<data_.size());
        data_[i].events = events;
    }
    void unsubscribe(PollHandle& handle) override {
        del(handle.fd());
    }
    void resubscribe(PollHandle& handle, PollEvents events) override {
        mod(handle.fd(), events);
    }

    int wait(std::error_code& ec) noexcept {
        int n = 0;
        for(std::size_t i=0; i<data_.size(); i++) {
            auto &q = *data_[i].queue;
            if(q) {
                auto ev = data_[i].events;
                if(q.size()>0) {
                    ev |= PollEvents::Read;
                }
                if(q.available()>0) {
                    ev |= PollEvents::Write;
                }
                data_[i].events = ev;
                if(ev!=PollEvents::None)
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
    struct Data {
        QueueT* queue;
        PollEvents events;
        IoSlot slot;        
    };
    std::vector<Data> data_;

};

using Qpoll = BasicQueuePoll<ipc::MagicRingBuffer>;
}
}