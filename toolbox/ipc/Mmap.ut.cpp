#include "Mmap.hpp"

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>
using namespace std;
using namespace toolbox;

BOOST_AUTO_TEST_SUITE(MmapSuite)
BOOST_AUTO_TEST_CASE(MmapAllocateMagicCase)
{
    MmapAllocator<int> alloc(MmapFlags::Magic);
    int n = 4096;
    int*p = alloc.allocate(n); // 4*4096
    for(int i=0;i<2*n;i++) {
        p[i] = i;
        BOOST_CHECK_EQUAL(p[i%n], i);
        BOOST_CHECK_EQUAL(p[i], i);
    }
    std::cout << "path: "<<alloc.path()<<"\n";
    alloc.deallocate(p, n);
}

BOOST_AUTO_TEST_CASE(MmapAllocateNoMagicCase)
{
    MmapAllocator<int> alloc;
    int n = 4096;
    int*p = alloc.allocate(n); // 4*4096
    for(int i=0;i<n;i++) {
        p[i] = i;
        BOOST_CHECK_EQUAL(p[i], i);
    }        
    std::cout << "path: "<<alloc.path()<<"\n";
    alloc.deallocate(p, n);
}

BOOST_AUTO_TEST_CASE(MmapAllocateMagicSharedCase)
{
    MmapAllocator<int> alloc(MmapFlags::Magic | MmapFlags::Shared);
    int n = 4096;
    int*p = alloc.allocate(n); // 4*4096
    for(int i=0;i<2*n;i++) {
        p[i] = i;
        BOOST_CHECK_EQUAL(p[i%n], i);
        BOOST_CHECK_EQUAL(p[i], i);
    }
    std::cout << "path: "<<alloc.path()<<"\n";
    alloc.deallocate(p, n);
}
BOOST_AUTO_TEST_CASE(MmapAllocateMagicHeaderCase)
{
    MmapAllocator<int> alloc{MmapFlags::Magic, 4096};
    int n = 4096;
    int*p = alloc.allocate(n+1024);
    for(int i=0;i<2*n;i++) {
        p[1024+i] = i;
        BOOST_CHECK_EQUAL(p[1024+(i%n)], i);
        BOOST_CHECK_EQUAL(p[1024+i], i);
    }
    std::cout << "path: "<<alloc.path()<<"\n";
    alloc.deallocate(p, n);
}
BOOST_AUTO_TEST_SUITE_END()