#pragma once
#include <aeron/aeronc.h>
#include "toolbox/aeron/Aeron.hpp"
#include <system_error>
#include <toolbox/util/Slot.hpp>

namespace toolbox {
inline namespace aeron {

class AeronContext;

class AeronSocket: public AeronPub, public AeronSub {
public:
    using Endpoint = typename AeronPub::Endpoint;
public:
    template<class ReactorT>
    AeronSocket(ReactorT& r)
    : AeronPub {r}
    , AeronSub {r}
    {}
    
    AeronSocket() = default;

    template<class ReactorT>
    void open(ReactorT& r) {
        AeronPub::open(r);
        AeronSub::open(r);
    }
    void close() {
        AeronPub::close();
        AeronSub::close();
    }

    /*void on_io_event(CyclTime now, int fd, PollEvents events) {
        AeronSub::on_io_event(now, fd, events);
        AeronSub::on_io_event(now, fd, events);
    }*/
private:
    //AeronSub sub_;
};

}
}