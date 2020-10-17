#pragma once

#include <string_view>
#include <string>

namespace toolbox{ inline namespace util {
class InternedStrings {
public:
    std::string_view intern(std::string_view str) {
        //std::cout << strings_<<"|"<<str<<std::endl;
        std::size_t pos = buffer_.find(str.data(), 0, str.size()+1);
        if(pos!=std::string::npos) {
            return std::string_view(buffer_.data()+pos, str.size());
        }
        std::size_t len = buffer_.size();
        buffer_ += str; 
        buffer_ += '\0';
        std::string_view result {buffer_.data() + len, str.size()};
        return result;
    }
    void reserve(std::size_t capacity) { buffer_.reserve(capacity); }
    void clear() { buffer_.clear(); }
private:
    std::string buffer_;
};

} // ns util
} // ns toolbox