#include "toolbox/net/Pcap.ipp"

#include <stdexcept>
#include <cassert>


using namespace toolbox::net;

void PcapDevice::open()
{
    char errbuf[PCAP_ERRBUF_SIZE];
    handle_ = pcap_open_offline(input_.data(), errbuf);
    assert(handle_!=nullptr);
}

void PcapDevice::close()
{
    if(handle_!=nullptr)
        pcap_close(handle_);
    handle_ = nullptr;
}


void PcapDevice::pcap_packet_handler(u_char *userData, const struct pcap_pkthdr* pkthdr, const u_char* packet)
{
    ether_header *ethhdr = (ether_header*)packet;
    if(ntohs(ethhdr->ether_type) == ETHERTYPE_IP)
        reinterpret_cast<PcapDevice*>(userData)->packets_(PcapPacket(pkthdr, packet));
    // NOTE: non-IP packets are dropped silently
}

int PcapDevice::loop()
{
    assert(handle_!=nullptr);

    int rc = pcap_loop(handle_, max_packet_count_, pcap_packet_handler, (u_char*) this);
    if(rc>=0) {
        return rc;
    }
    switch(rc) {
        case -1: throw PcapError(std::string(pcap_geterr(handle_)));
        case -2: return rc; // break called
        default: throw PcapError("invalid pcap return code");
    }
}