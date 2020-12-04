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
#include <toolbox/util/Slot.hpp>
#include "toolbox/http/Conn.hpp"
#include "toolbox/http/Request.hpp"
#include "toolbox/http/Serv.hpp"
#include "toolbox/io/Runner.hpp"
#include <toolbox/http.hpp>

#include <toolbox/io.hpp>
#include <toolbox/sys.hpp>
#include <toolbox/util.hpp>

using namespace std;
using namespace toolbox;

namespace {

namespace tb = toolbox;
class HttpApp {
  public:
    using Slot = BasicSlot<const HttpRequest&, HttpStream&>;
    using SlotMap = RobinMap<std::string, Slot>;
    using Conn = HttpServerConn;
    using Endpoint = typename Conn::Endpoint;
    using This = HttpApp;
  public:
    HttpApp(HttpServ *serv)
    : serv_(serv) {
        serv_->accept(tb::bind<&This::on_accept>(this));
    }
    void http_request(const std::string& path, Slot slot) { slot_map_[path] = slot; }

  protected:
    void on_accept(CyclTime now, Conn& conn) {
        conn.http_message(tb::bind<&This::on_http_message>(this));
    }
    void on_http_message(CyclTime now, const Endpoint& ep, const HttpRequest& req, HttpStream& os)
    {
        const auto it = slot_map_.find(string{req.path()});
        if (it != slot_map_.end()) {
            os.http_status(HttpStatus::Ok, TextPlain);
            it->second(req, os);
        } else {
            os.http_status(HttpStatus::NotFound, TextPlain);
            os << "Error 404 - Page not found";
        }
        os.commit();
    }

  private:
    SlotMap slot_map_;
    HttpServ* serv_{};
};


void on_foo(const HttpRequest& req, HttpStream& os)
{
    os << "Hello, Foo!";
}

void on_bar(const HttpRequest& req, HttpStream& os)
{
    os << "Hello, Bar!";
}

} // namespace

int main(int argc, char* argv[])
{
    int ret = 1;
    try {

        const auto start_time = CyclTime::now();

        Reactor reactor{1024};
        
        const TcpEndpoint ep{TcpProtocol::v4(), 8888};
        HttpServ http_serv{start_time, reactor, ep};

        HttpApp app {&http_serv};

        app.http_request("/foo", bind<on_foo>());
        app.http_request("/bar", bind<on_bar>());

        // Start service threads.
        Reactor::Runner reactor_runner{reactor, "reactor"s};
        
        // wait SIGTERM from user
        pthread_setname_np(pthread_self(), "main");
        wait_termination_signal();

    } catch (const std::exception& e) {
        TOOLBOX_ERROR << "exception: " << e.what();
    }
    return ret;
}
