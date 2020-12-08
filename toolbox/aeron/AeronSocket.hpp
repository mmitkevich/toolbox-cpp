#pragma once
#include <aeron/aeronc.h>
#include "toolbox/aeron/Aeron.hpp"
#include <system_error>
#include <toolbox/util/Slot.hpp>

namespace toolbox {
inline namespace aeron {

class AeronContext;


class AeronSocket: public AeronPub {
public:
    using Endpoint = AeronEndpoint;
    using Base = AeronPub;
public:
    template<class ReactorT>
    AeronSocket(ReactorT& r)
    : Base {r} {}
    
    AeronSocket() = default;

    template<class ReactorT>
    void open(ReactorT& r) {
        aeron_ = & r.template get<AeronPoll>().aeron();
        Base::open(aeron_);
    }

    void on_io_event(CyclTime now, os::FD fd, PollEvents events) {
        //sub_.on_io_event(now, fd, events);
        Base::on_io_event(now, fd, events);
    }
private:
    //AeronSub sub_;
};

}
}