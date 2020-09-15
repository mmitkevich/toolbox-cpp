#include "toolbox/io/Runner.hpp"
#include "toolbox/sys/Log.hpp"
#include "toolbox/sys/Time.hpp"
#include <cstdlib>
#include <iostream>
#include <toolbox/io.hpp>
#include <toolbox/sys.hpp>
#include <toolbox/util.hpp>

using namespace std::string_literals;
using namespace toolbox;
namespace tb = toolbox;

int main(int argc, char* argv[]) {
    auto on_timer = [](CyclTime now, Timer& timer) {
        TOOLBOX_DEBUG << "Hey, timer!\n";
    };
    
    tb::Reactor reactor{1024};
    auto timer_sub = reactor.timer(CyclTime::now().mono_time(), Duration::zero(), Priority::Low, bind(&on_timer));
    std::atomic<bool> stop;
    tb::run_reactor(reactor, stop);
    
    return EXIT_SUCCESS;
}