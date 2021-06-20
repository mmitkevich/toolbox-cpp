#include <utility>

namespace toolbox {
inline namespace ranges {

    template<class R>
    auto begin(R r) { return std::begin(std::forward<R>(r)); }
    
    template<class R>
    auto end(R r) { return std::begin(std::forward<R>(r)); }
    
    template <class R>
    using iterator_t = decltype(std::begin(std::declval<R&>()));

    template <class  R>
    using sentinel_t = decltype(std::end(std::declval<R&>()));
} // ranges
} // toolbox

