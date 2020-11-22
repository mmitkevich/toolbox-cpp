#include "MagicRingBuffer.hpp"

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <sstream>
#include <thread>

#include <iostream>

using namespace std;
using namespace toolbox;

BOOST_AUTO_TEST_SUITE(MRBSuite)

BOOST_AUTO_TEST_CASE(MRBNumeric)
{
    MagicRingBuffer mrb(PageSize);
    static constexpr std::size_t rec_size = 3;
    static constexpr std::size_t N = 10'000;
    std::size_t rtotal {};
    std::size_t nempty {};    
    std::thread runner([&] {
        bool stop = false;
        std::size_t i = 1;
        while(!stop) {
            std::size_t v{};
            bool res = false;
            if constexpr(rec_size==sizeof(v)) {
                res = mrb.read(v);
            }else {
                res = mrb.read((char*)&v, rec_size);
            }
            if(res) {
                //std::cout << "read="<<v<<std::endl;
                if(v==0)
                    stop = true;
                else if(v!=i) {
                    std::cout <<"r expected "<<i<<" found "<<v<<std::endl;
                } else 
                    rtotal+=v;
                i++;
            }else {
                nempty++;
            }
        }
    });

    std::size_t wtotal {};
    std::size_t nfull {};

    for(std::size_t i=1;i<=N;i++) {
        wtotal+=i;
        for(;;) {
            bool res = false;
            if constexpr(sizeof(i)==rec_size) {
                res = mrb.write(i);
            } else {
                res = mrb.write(&i, rec_size);
            }
            if(!res)
                nfull++;
            else
                break;
        }
    }
    std::size_t zero = 0;
    while(!mrb.write(zero)) {
        //std::cout << "full:"<<mrb.available()<<"\n";
    }   
    runner.join();
    std::cout << "w total="<<wtotal<<" theo total="<< (1+N)*N/2 <<" nfull="<<nfull<<std::endl;
    std::cout << "r total="<<rtotal<<" nempty="<<nempty<<std::endl;        
    BOOST_CHECK_EQUAL(rtotal, wtotal);    
    BOOST_CHECK_EQUAL(rtotal, (1+N)*N/2);
}

BOOST_AUTO_TEST_CASE(MRBString)
{
    MagicRingBuffer mrb(PageSize);
    static constexpr std::size_t N = 1'000;
    std::size_t nempty {};    
    std::thread runner([&] {
        bool stop = false;
        while(!stop) {
            if(!mrb.read(0, [&](const char* buf, std::size_t size) noexcept {
                auto val = std::string_view{buf, size};
                std::cout << "read["<<size<<"]="<<val<<std::endl;
                if(val.find("stop")!=std::string::npos)
                    stop = true;
                return size;
            })) {
                nempty++;
            }
        }
    });

    std::size_t nfull {};

    for(std::size_t i=1;i<=N;i++) {
        for(;;) {
            std::stringstream ss;
            ss<<"tst"<<i;
            if(!mrb.write(std::string_view{ss.str()}))
                nfull++;
            else
                break;
        }
    }
    while(!mrb.write(std::string_view{"stop"})) {
        //std::cout << "full:"<<mrb.available()<<"\n";
    }   
    runner.join();
    std::cout << "nfull="<<nfull<<" nempty="<<nempty<<std::endl;        
}
BOOST_AUTO_TEST_SUITE_END()