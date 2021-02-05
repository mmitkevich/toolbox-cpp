// std replacements

#ifdef USE_TL_OPTIONAL
#include "contrib/tl/optional.hpp"
namespace toolbox {
    template<typename T>
    using optional = tl::optional<T>;
}
#else
#include <boost/optional.hpp>
namespace toolbox {
    template<typename T>
    using optional = boost::optional<T>;
}
#endif
