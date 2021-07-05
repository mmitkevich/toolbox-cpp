#pragma once

#include <cstring>
#include <string_view>
#include <string>
#include <toolbox/util/RobinHood.hpp>

namespace toolbox{ inline namespace util {
class InternedStrings {
public:
    ~InternedStrings() { clear(); }

    std::string_view intern(std::string_view str) {
        //std::cout << strings_<<"|"<<str<<std::endl;
        auto hash = std::hash<std::string_view>{}(str);
        auto it = values_.find(str);
        if(it!=values_.end()) {
            return it->first;
        } else {
            char* data = new char[str.size()+1];
            std::memcpy(data, str.data(), str.size());
            data[str.size()] = '\0';
            auto [it, is_new] = values_.emplace(data, Empty{});
            return it->first;
        }
    }
    
    void clear() { 
        for(auto [k,v] : values_) {
            delete k.data();
        }
        values_.clear(); 
    }
private:
    struct Empty{};
    util::RobinFlatMap<std::string_view, Empty> values_;
};

} // ns util
} // ns toolbox