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

#ifndef TOOLBOX_IO_RUNNER_HPP
#define TOOLBOX_IO_RUNNER_HPP

#include <toolbox/sys/Thread.hpp>
#include <toolbox/sys/Log.hpp>
#include <toolbox/sys/Signal.hpp>

#include <atomic>
#include <thread>
#include <csignal>

namespace toolbox {
inline namespace io {


template<typename CallableT>
inline void run_thread(CallableT& r, ThreadConfig config)
{
    sig_block_all();
    try {
        set_thread_attrs(config);
        TOOLBOX_NOTICE << "started " << config.name << " thread";
        r();
    } catch (const std::exception& e) {
        TOOLBOX_CRIT << "exception: " << e.what();
        kill(getpid(), SIGTERM);
    }
    TOOLBOX_NOTICE << "stopping " << config.name << " thread";
}

template<class ReactorT, class InitFn>
class TOOLBOX_API BasicRunner {
  public:
    /// Start new thread and run
    BasicRunner(ReactorT& r, InitFn &&init, ThreadConfig config = std::string{"reactor"})
    : reactor_{r}
    , init_{init}
    , thread_{run_thread<BasicRunner<ReactorT, InitFn>>, std::ref(*this), config}
    { }

    ~BasicRunner() {
        reactor_.stop();
        reactor_.wakeup();
        thread_.join();
    }

    void operator()() {
        if(init_())
            reactor_.run();
        std::raise(SIGTERM);
    }
    // Copy.
    BasicRunner(const BasicRunner&) = delete;
    BasicRunner& operator=(const BasicRunner&) = delete;

    // Move.
    BasicRunner(BasicRunner&&) = delete;
    BasicRunner& operator=(BasicRunner&&) = delete;

  private:
    ReactorT& reactor_;
    InitFn init_;
    std::thread thread_;
};

void TOOLBOX_API wait_termination_signal();

} // namespace io
} // namespace toolbox

#endif // TOOLBOX_IO_RUNNER_HPP
