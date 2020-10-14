#include "Pool.hpp"
#include <boost/test/unit_test.hpp>
using namespace toolbox::util;


BOOST_AUTO_TEST_SUITE(PoolSuite)

struct Test {
    Test() {
        value = 1;
    }
    int value;
};

BOOST_AUTO_TEST_CASE(PoolBasic)
{
    Pool<Test> pool;
    auto ptr = pool.alloc();

    BOOST_TEST(1 == ptr->value);
    pool.dealloc(ptr);

}

BOOST_AUTO_TEST_SUITE_END()
