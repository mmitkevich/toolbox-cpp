#include "Xml.hpp"
#include <iostream>

#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace toolbox;

BOOST_AUTO_TEST_SUITE(XmlSuite)

BOOST_AUTO_TEST_CASE(XmlParse)
{
    xml::Document doc;
    xml::Parser p;
    doc = p.parse_file("/opt/tbricks/instr-2020-10-26.xml");
    for(auto &e : doc.child("exchange").child("traded_instruments")) {
        std::cout <<"type:"<<e.name()<<",venue_instrument_id:"<<std::atoi(e.attribute("instrument_id").value())<<",symbol:"<<e.attribute("symbol").value()<<std::endl;
    }
}
BOOST_AUTO_TEST_SUITE_END()
