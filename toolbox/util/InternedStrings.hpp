#pragma once

#include <string_view>
#include <string>
#include <toolbox/util/RobinHood.hpp>

namespace toolbox{ inline namespace util {
class InternedStrings {
public:
    std::string_view intern(std::string_view str) {
        //std::cout << strings_<<"|"<<str<<std::endl;
        auto hash = std::hash<std::string_view>{}(str);
        auto it = index_.find(hash);
        if (it==index_.end()) {
            auto len = buffer_.size();
            buffer_ += str;
            buffer_ += '\0';
            auto result = std::string_view{buffer_.data()+len, str.size()};
            index_[hash] = result;
            return result;
        } else {
            return it->second;
        }
    }
    void reserve(std::size_t capacity) { buffer_.reserve(capacity); }
    void clear() { buffer_.clear(); }
private:
    std::string buffer_;
    util::RobinFlatMap<std::size_t, std::string_view> index_;
};

} // ns util
} // ns toolbox