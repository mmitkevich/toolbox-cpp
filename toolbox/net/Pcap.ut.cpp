#include "toolbox/net/Pcap.hpp"
#include "toolbox/sys/Log.hpp"
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/hex.hpp>

#include <sstream>
#include <string_view>
#include <unordered_map>

using namespace toolbox;

std::string to_hex(std::string_view sv) {
    std::string result(sv.size()*2,'\0');
    boost::algorithm::hex(sv.begin(), sv.end(), result.begin());
    return result;
}

BOOST_AUTO_TEST_SUITE(PcapSuite)
BOOST_AUTO_TEST_CASE(PcapFile)
{
    const char* PCAP_FILE = "./test.pcap";
    TOOLBOX_INFO << "reading "<<PCAP_FILE;
    PcapDevice pcap;
    pcap.input(PCAP_FILE);
    pcap.open();
    pcap.max_packet_count(100000);
    struct filter {
        std::string host;
        u_int port;
    };
    filter filter { "233.26.38.16", 6016 };
    assert(filter.host==std::string("233.26.38.16"));
    std::size_t nfound = 0;
    std::unordered_map<std::string, ulong> host_stats;

    pcap.packet([&](const PcapPacket& pkt) {
        //TOOLBOX_INFO << pkt;
        std::string  dst_host = pkt.dst_host();
        host_stats[dst_host+":"+std::to_string(pkt.dst_port())]++;
        if(dst_host==filter.host /*&& pkt.dst_port()==dst_port*/) {
            TOOLBOX_INFO << pkt << " | " << to_hex(std::string_view(pkt.data(), pkt.size()));
            ++nfound;
        }
    });
    pcap.loop();
    TOOLBOX_INFO << "Found: "<<nfound<<" packets";
    //for(auto& [host,count]: host_stats) {
    //    TOOLBOX_INFO<<host<<" : "<<count; 
    //}
}
BOOST_AUTO_TEST_SUITE_END()
