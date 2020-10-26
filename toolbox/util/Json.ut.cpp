#include "Json.hpp"
#include <boost/test/tools/old/interface.hpp>
#include <iostream>

#include <boost/test/unit_test.hpp>
#include <json/simdjson.h>
#include <sstream>

using namespace toolbox;
using namespace toolbox::json;

BOOST_AUTO_TEST_SUITE(JsonSuite)

BOOST_AUTO_TEST_CASE(JsonParse)
{
    std::cout <<"\nJsonParse:\n";
    try {
        Parser parser;
        std::string json = "{\"n\":1, \"s\":\"abc\", \"a\":[1,2,3], \"o\":{\"x\":\"XX\"}}";
        Element d = parser.parse(json);
        Int n = d["n"];
        BOOST_CHECK_EQUAL(n,1);
        StringView s = d["s"];
        BOOST_CHECK_EQUAL(s,"abc");
        int i = 0;
        for(auto e : d["a"]) {
            Int x = e;
            BOOST_CHECK_EQUAL(x, ++i);
        }
        Object o = d["o"];
        for(auto [k,v]: o) {
            std::cout << k <<" = "<<v<<std::endl;
            BOOST_CHECK_EQUAL(k,"x");
            std::string_view sv = v;
            BOOST_CHECK_EQUAL(sv, "XX");
        }
        std::cout << "a=" << d["a"] << std::endl
            << "n=" << d["n"] << std::endl
            << "s=" << d["s"] << std::endl;
    }catch(Error& e) {
        std::cerr << e.what() << std::endl;
    }
} 

BOOST_AUTO_TEST_CASE(JsonDocument)
{
    std::cout<<"\nJsonDocument:\n";
    
    MutableDocument d; // all allocations are in Document
    
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
                {{"key"_jk, "value"}}};
#define T(a,b) a=b; BOOST_CHECK_EQUAL(a, MutableElement(b))
    // mutation
    T(d["n"], 1);
    T(d["n"], 2);
    T(d["s"], "AAA");
    T(d["s"], "AAAA");
    T(d["o"]["x"], "XXX");
    T(d["o"]["y"],"YYY");
    T(d["a"][0], 1);
    T(d["a"][1], 2);
    T(d["a"][2], 3);
    std::cout << "d[a]="<<d["a"] <<"\n";
    T(d["a"][4], 5);
    
    T(d["t1"], 1);
    d.erase("t1");
    d["t2"] = {0,1,2,3};
    for(std::size_t i=0;i<=3;i++)
        BOOST_CHECK_EQUAL(d["t2"][i], MutableElement((int)i));
    d["t2"].erase(1,3);
    BOOST_CHECK_EQUAL(d["t2"][0], 0);
    BOOST_CHECK_EQUAL(d["t2"][1], 3);
    BOOST_CHECK_EQUAL(d["t2"].size(), 2);
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
BOOST_AUTO_TEST_CASE(JsonParseMutable)
{
    std::cout<<"\nJsonParseMutable:\n";
    std::string json = "{\"n\":1,\"s\":\"abc\",\"a\":[1,2,3],\"o\":{\"x\":\"XX\"}}";
    std::cout<<"original:\n"<<json<<"\nconverted:\n";
    Parser p;
    MutableDocument d; // Document allocates memory, but does not free it until destructed
    simdjson::dom::element pj = p.parse(json);
    MutableDocument::copy(pj, d);
    std::cout << d;
    std::stringstream ss;
    ss << d;
    BOOST_CHECK_EQUAL(ss.str(), json);
}
BOOST_AUTO_TEST_CASE(JsonComments)
{
    std::string js = "[\"js\non\"//comments\n,3]";
    MutableDocument::cpp_comments_to_whitespace(js);
    std::cout << "\n\ndecommentized:\n"<<js;
}
BOOST_AUTO_TEST_SUITE_END()
