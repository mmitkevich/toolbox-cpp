#include "Json.hpp"
#include <iostream>

#include <boost/test/unit_test.hpp>

using namespace toolbox;
using namespace toolbox::json;

BOOST_AUTO_TEST_SUITE(JsonSuite)

BOOST_AUTO_TEST_CASE(JsonRead)
{
    std::cout <<"\nJsonRead:\n";
    try {
        Parser parser;
        std::string json = "{\"n\":1, \"s\":\"abc\", \"a\":[1,2,3]}";
        ElementView d = parser.parse(json);
        Int n = d["n"];
        BOOST_CHECK_EQUAL(n,1);
        StringView s = d["s"];
        BOOST_CHECK_EQUAL(s,"abc");
        int i = 0;
        for(auto e : d["a"]) {
            Int x = e;
            BOOST_CHECK_EQUAL(x, ++i);
        }
        std::cout << "a=" << d["a"] << std::endl
            << "n=" << d["n"] << std::endl
            << "s=" << d["s"] << std::endl;
    }catch(Error& e) {
        std::cerr << e.what() << std::endl;
    }
} 

BOOST_AUTO_TEST_CASE(JsonTree)
{
    std::cout<<"\nJsonTree:\n";
    
    Document d; // all allocations are in Document
    
    // array initialization
    d["ai"] = {"1", 2, true, 3.14, -10};
    
    // object initialization
    using namespace json::literals;
    d["oi"] = {{"aa"_jk, "AA"},
               {"bb"_jk, "BB"},
               {"cc"_jk, "CC"}};

    // complex array initialization
    d["ci"] = { 1, 
                {2, 3}, 
                Element {{"key"_jk, "value"}}};

    // mutation
    d["n"] = 1;
    d["n"] = 2;
    d["s"] = "AAA";
    d["s"] = "AAAA";
    d["o"]["x"] = "XXX";
    d["o"]["y"] = "YYY";
    d["a"][0] = 1;
    d["a"][1] = 2;
    d["a"][2] = 3;
    std::cout << "d[a]="<<d["a"] <<"\n";
    d["a"][4] = 5;
    
    d["t1"] = 1;
    d.erase("t1");
    d["t2"] = {0,1,2,3};
    d["t2"].erase(1,3);
    d["t3"] = {0};
    d["t3"].erase(0);
    d["t4"] = {0,1};
    d["t4"].erase(1);

    std::cout<<"enumerate d as key value:\n";
    for(auto [k, e]: d) {
        std::cout << std::setw(8)<<k<<"=";
        e.print(std::cout,8)<<std::endl;
    }
    std::cout << "d=\n"<<d << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
