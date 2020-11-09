#include "Generator.hpp"

#include <boost/test/unit_test.hpp>

namespace tbu=toolbox::util;

struct some_resource {
    int x;
    explicit some_resource(int x) : x(x) {
        std::printf("some_resource(%i)\n", x);
    }
    ~some_resource() {
        std::printf("~some_resource(%i)\n", x);
    }
};

static tbu::generator<const uint64_t> fib(int max) {
    some_resource r{2};
    auto a = 0, b = 1 ;
    for(auto n = 0; n <  max; n ++)  {
        co_yield b;
        const auto next = a + b;
        a = b, b = next;
    }
}


static tbu::generator<const uint64_t> test(int max) {
    auto g = fib(max);
    some_resource r{1};
    co_yield std::move(g);    
}


BOOST_AUTO_TEST_SUITE(GeneratorSuite)

BOOST_AUTO_TEST_CASE(GeneratorIterate)
{
    //setbuf(stdout, NULL);

#if __has_include(<ranges>)
    auto v = test(10) | std::views::drop(9);
    return *std::ranges::begin(v);
#else
    uint64_t  i = 0;
    for(auto a : test(10)) {
        i = a;
    }
    return i;
#endif
}
BOOST_AUTO_TEST_SUITE_END()


