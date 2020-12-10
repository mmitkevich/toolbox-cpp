#pragma once

#include <unordered_map>
#include <string>
#include <exception>
#include <sstream>

namespace toolbox {
    inline namespace net {

/// url(|key=value)*
class ParsedUrl {
public:
    using UrlParams = std::unordered_map<std::string_view, std::string_view>;
public:
    ParsedUrl() = default;
    ParsedUrl(std::string_view s, std::string_view proto_sep="://", 
            std::string_view query_sep="?", std::string_view service_sep=":", std::string_view params_sep="|") {
        parse(s, params_sep, proto_sep, query_sep, service_sep);
    }
    void parse(std::string_view s, std::string_view params_sep="|", std::string_view proto_sep="://", 
            std::string_view query_sep="?", std::string_view service_sep=":") {
        data_ = s;
        url_ = data_;
        std::size_t p;
        if(!params_sep.empty()) {
            auto l = s.size();
            p = s.find(params_sep);
            if(p==std::string_view::npos)
                p = l;
            url_ = s.substr(0, p);
            while(p!=std::string_view::npos && p<l) {
                auto e = s.find('=', p);
                if (e == std::string_view::npos)
                    throw std::logic_error("ParsedUrl: = expected");
                auto key = s.substr(p+1, e-p-1);
                p = s.find(params_sep, e);
                if(p == std::string_view::npos)
                    p = s.size();
                auto val = s.substr(e+1, p-e-1);
                params_.emplace(key, val);
            }
        }
        std::string_view u = url_;
        proto_ = {};        
        if(!proto_sep.empty()) {
            p = u.find(proto_sep);
            if(p==std::string_view::npos) {
                proto_ = {};
            } else {         
                proto_ = u.substr(0, p);
                u = u.substr(p);
            }
        }
        query_ = {};
        if(!query_sep.empty()) {
            p = u.find(query_sep);
            if(p!=std::string_view::npos) {
                query_ = u.substr(p);
                u = u.substr(0, p);
            }
        }
        service_ = {};
        host_ = u;        
        if(!service_sep.empty()) {
            p = u.rfind(service_sep);
            if(p!=std::string_view::npos) {
                service_ = u.substr(p+1);
                host_ = u.substr(0, p);
            }
        }
    }
    const UrlParams& params() const { return params_; }
    std::string_view url() const { return url_; }
    const std::string& str() const {
        return data_;
    }
    std::string_view param(std::string_view key, std::string_view dflt={}) const {
        auto it = params_.find(key);
        return it!=params_.end() ? it->second : dflt;
    }
    std::string_view proto() const { return proto_; }
    std::string_view host() const { return host_; }
    std::string_view service() const { return service_; }
    std::string_view query() const { return query_; }
private:
    std::string data_;
    std::string_view url_;
    std::string_view proto_;
    std::string_view host_;
    std::string_view service_;
    std::string_view query_;
    UrlParams params_; 
};

} // namespace net
} // namespace toolbox