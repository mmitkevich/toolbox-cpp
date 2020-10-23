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

#include "Runner.hpp"

#include "Reactor.hpp"

#include <toolbox/sys/Log.hpp>
#include <toolbox/sys/Signal.hpp>

namespace toolbox {
inline namespace io {

void run_reactor_thread(Reactor& r, ThreadConfig config)
{
    sig_block_all();
    try {
        set_thread_attrs(config);
        TOOLBOX_NOTICE << "started " << config.name << " thread";
        r.run();
    } catch (const std::exception& e) {
        TOOLBOX_CRIT << "exception: " << e.what();
        kill(getpid(), SIGTERM);
    }
    TOOLBOX_NOTICE << "stopping " << config.name << " thread";
}

ReactorRunner::ReactorRunner(Reactor& r, ThreadConfig config)
: reactor_{r}
, thread_{run_reactor_thread, std::ref(r), config}
{
}

ReactorRunner::~ReactorRunner()
{
    reactor_.stop();
    reactor_.wakeup();
    thread_.join();
}

// Wait for termination.
void wait_termination_signal() {
    SigWait sig_wait;
    for (;;) {
        try {
        switch (const auto sig = sig_wait()) {
            case SIGHUP:
                TOOLBOX_INFO << "received SIGHUP";
                continue;
            case SIGINT:
                TOOLBOX_INFO << "received SIGINT";
                break;
            case SIGTERM:
                TOOLBOX_INFO << "received SIGTERM";
                break;
            default:
                TOOLBOX_INFO << "received signal: " << sig;
                continue;
            }
            break;
        }catch(const std::exception& e) {
            TOOLBOX_ERROR << "exception: " << e.what();        
            continue;
        }
    }
}

} // namespace io
} // namespace toolbox
