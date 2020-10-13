#include "JsonView.hpp"
#include <iostream>

#include <boost/test/unit_test.hpp>

using namespace toolbox;
using namespace toolbox::jsonview;

BOOST_AUTO_TEST_SUITE(JsonViewSuite)

BOOST_AUTO_TEST_CASE(JsonViewRead)
{
    try {
        JsonParser parser;
        std::string json = "{\"n\":1, \"s\":\"abc\", \"a\":[1,2,3]}";
        JsonElement d = parser.parse(json);
        JsonInt n = d["n"];
        BOOST_CHECK_EQUAL(n,1);
        JsonStringView s = d["s"];
        BOOST_CHECK_EQUAL(s,"abc");
        int i = 0;
        for(auto e : d["a"]) {
            JsonInt x = e;
            BOOST_CHECK_EQUAL(x, ++i);
        }
        std::cout << "a=" << d["a"] << std::endl
            << "n=" << d["n"] << std::endl
            << "s=" << d["s"] << std::endl;
    }catch(JsonError& e) {
        std::cerr << e.what() << std::endl;
    }
} 

BOOST_AUTO_TEST_SUITE_END()
