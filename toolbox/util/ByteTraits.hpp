#pragma once

#include <cstdint>
#include <utility>
#include <boost/type_traits/is_detected.hpp>

#define TB_FIELD_PTR(x, n) reinterpret_cast<decltype(x)*>(((char*)&x)+n)
#define TB_FIELD_CPTR(x, n) reinterpret_cast<const decltype(x)*>(((const char*)&x)+n)

#define TB_FIELD_FN(type, name, body) \
    type& name() body; \
    const type& name() const body;

/// additional space in bytes in the field
#define TB_PADSIZE(name) ((std::ptrdiff_t)(toolbox::util::ByteTraits::bytesize(name)-sizeof(name)))

#define TB_FIELD(type, name, field) \
    type field; \
    type& name() { return *TB_FIELD_PTR(field, TB_PADVAL); }; \
    const type& name() const { return *TB_FIELD_CPTR(field, TB_PADVAL); };

#define TB_BYTESIZE(type) \
    constexpr std::size_t bytesize() const noexcept { return sizeof(type) + TB_PADVAL; }


namespace toolbox { inline namespace util {

struct ByteTraits {
    template<typename T> 
    using bytesize_t = decltype(std::declval<T&>().bytesize());
    template<typename T>
    constexpr static bool has_bytesize = boost::is_detected_v<bytesize_t, T>;
    template<typename T>
    static constexpr std::size_t bytesize(const T& val) noexcept {
        if constexpr(has_bytesize<T>) {
            return val.bytesize();
        } else {
            return sizeof(val);
        }
    }
};

template<typename T>
constexpr auto byte_sizeof(const T& val) { return ByteTraits::bytesize(val); }

} } // ft::utils