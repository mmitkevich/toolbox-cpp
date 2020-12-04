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

#ifndef TOOLBOX_HTTP_REQUEST_HPP
#define TOOLBOX_HTTP_REQUEST_HPP

#include "toolbox/http/Stream.hpp"
#include <toolbox/http/Types.hpp>
#include <toolbox/http/Url.hpp>

#include <vector>

namespace toolbox {
inline namespace http {


class TOOLBOX_API HttpRequest : public BasicUrl<HttpRequest> {
  public:
    HttpRequest() = default;
    ~HttpRequest();

    // Copy.
    HttpRequest(const HttpRequest&) = delete;
    HttpRequest& operator=(const HttpRequest&) = delete;

    // Move.
    HttpRequest(HttpRequest&&) = delete;
    HttpRequest& operator=(HttpRequest&&) = delete;

    HttpMethod method() const noexcept { return method_; }
    const std::string& url() const noexcept { return url_; }
    const HttpHeaders& headers() const noexcept { return headers_; }
    const std::string& body() const noexcept { return body_; }

    void clear() noexcept
    {
        method_ = HttpMethod::Get;
        url_.clear();
        headers_.clear();
        body_.clear();
    }
    void flush() { parse(); }
    void set_method(HttpMethod method) noexcept { method_ = method; }
    void append_url(std::string_view sv) { url_.append(sv.data(), sv.size()); }
    void append_header_field(std::string_view sv, First first)
    {
        if (first == First::Yes) {
            headers_.emplace_back(std::string{sv.data(), sv.size()}, "");
        } else {
            headers_.back().first.append(sv.data(), sv.size());
        }
    }
    void append_header_value(std::string_view sv, First first)
    {
        headers_.back().second.append(sv.data(), sv.size());
    }
    void append_body(std::string_view sv) { body_.append(sv.data(), sv.size()); }

  private:
    HttpMethod method_{HttpMethod::Get};
    std::string url_;
    HttpHeaders headers_;
    std::string body_;
};

inline std::ostream& operator<<(std::ostream& os, const HttpRequest& req) {
    os << enum_string(req.method()) << ", url=" << req.url();
    for(auto&[k,v]: req.headers()) {
        os << ", hdr: "<< k << ":" << v;
    }
    return os;
}

class TOOLBOX_API HttpResponse {
  public:
    HttpResponse() = default;
    ~HttpResponse() = default;

    // Copy.
    HttpResponse(const HttpResponse&) = delete;
    HttpResponse& operator=(const HttpResponse&) = delete;

    // Move.
    HttpResponse(HttpResponse&&) = delete;
    HttpResponse& operator=(HttpResponse&&) = delete;
  public:
    void reset() {
        headers_.clear();
        status_ = {};
    }
    const HttpHeaders& headers() const noexcept { return headers_; }
    const std::string& status() const noexcept { return status_; }
    void status(std::string_view val) { status_ = val; }
  private:
    HttpHeaders headers_;
    std::string status_;
};

} // namespace http
} // namespace toolbox

#endif // TOOLBOX_HTTP_REQUEST_HPP
