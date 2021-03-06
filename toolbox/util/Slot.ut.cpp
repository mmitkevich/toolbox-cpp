// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
// Copyright (C) 2020 Reactive Markets Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Slot.hpp"
#include <iostream>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace toolbox;

namespace {
void foo(int& x)
{
    x <<= 1;
}
} // namespace

// Test constexpr.
static_assert(BasicSlot<int>{} == BasicSlot<int>{});
static_assert(BasicSlot<int>{}.empty());
static_assert(!BasicSlot<int>{});

BOOST_AUTO_TEST_SUITE(SlotSuite)

BOOST_AUTO_TEST_CASE(SlotEmptyCase)
{
    BOOST_TEST(BasicSlot<int>{}.empty());
    BOOST_TEST(!BasicSlot<int>{});
}

BOOST_AUTO_TEST_CASE(SlotFreeFunCase)
{
    int x{2};
    auto cb = bind<foo>();
    cb(x);
    BOOST_TEST(x == 4);
    cb(x);
    BOOST_TEST(x == 8);
}

BOOST_AUTO_TEST_CASE(SlotFunctorCase)
{
    struct Test {
        void operator()() { x <<= 1; }
        int x{2};
    } t;

    auto cb = bind(&t);
    cb();
    BOOST_TEST(t.x == 4);
    cb();
    BOOST_TEST(t.x == 8);
}

BOOST_AUTO_TEST_CASE(SlotConstFunctorCase)
{
    struct Test {
        void operator()(int& x) const { x <<= 1; }
    } t;

    int x{2};
    auto cb = bind(&t);
    cb(x);
    BOOST_TEST(x == 4);
    cb(x);
    BOOST_TEST(x == 8);
}

BOOST_AUTO_TEST_CASE(SlotLambdaNullaryCase)
{
    int x{2};
    auto fn = [&x]() { x <<= 1; };

    auto cb = bind(&fn);
    cb();
    BOOST_TEST(x == 4);
    cb();
    BOOST_TEST(x == 8);
}

BOOST_AUTO_TEST_CASE(SlotLambdaUnaryCase)
{
    int x{2};
    auto fn = [](int& x) { x <<= 1; };

    auto cb = bind(&fn);
    cb(x);
    BOOST_TEST(x == 4);
    cb(x);
    BOOST_TEST(x == 8);
}

BOOST_AUTO_TEST_CASE(SlotMemFunCase)
{
    struct Test {
        void foo() { x <<= 1; }
        void bar(int n) { x += n; }
        int x{2};
    } t;

    auto cbfoo = bind<&Test::foo>(&t);
    cbfoo();
    BOOST_TEST(t.x == 4);

    auto cbbar = bind<&Test::bar>(&t);
    cbbar(3);
    BOOST_TEST(t.x == 7);
}

BOOST_AUTO_TEST_CASE(SlotConstMemFunCase)
{
    struct Test {
        void get(int& val) const { val = x; }
        int x{101};
    } t;

    auto cb = bind<&Test::get>(&t);
    int x{};
    cb(x);
    BOOST_TEST(x == t.x);
}

BOOST_AUTO_TEST_CASE(SlotRvalueFunCase)
{
    int x{2};
    auto fn = [&x](int&& y) { x = x + y; };

    BasicSlot<int&&> cb = toolbox::bind(&fn);
    cb(3);
    BOOST_TEST(x == 5);
    int&& m = 6;
    cb.invoke(std::move(m));
    BOOST_TEST(x == 11);
}

BOOST_AUTO_TEST_CASE(SlotSignalCase)
{
    Signal<int> sig;
    int x {2};
    auto fn1 = [&x](int y) { x+=y; };
    auto fn2 = [&x](int y) { x+=2*y; };
    sig.connect(toolbox::bind(&fn1));
    sig(5);
    BOOST_TEST(x==2+5);
    sig.connect(toolbox::bind(&fn2));
    sig(6);
    BOOST_TEST(x==2+5+3*6);
    sig.disconnect(toolbox::bind(&fn1));
    sig(7);
    BOOST_TEST(x==2+5+3*6+2*7);
}

BOOST_AUTO_TEST_CASE(SlotSmallLambdaCase)
{
    {
        struct Subject {
            int x{2};
        } s;
        Slot<int> slot = toolbox::bind([&s](int y) { 
            s.x = y;
        });
        slot(5);
        BOOST_TEST(s.x==5);
    }
    {
        Slot<int> slot = toolbox::bind([](int y) { 
            BOOST_TEST(y==6);
        });
        slot(6);
    }
    {
        char c = 12;
        Slot<int> slot = toolbox::bind([c](int y) { 
            BOOST_TEST(c==12);
            BOOST_TEST(y==6);
        });
        slot(6);
    }
    #ifdef WONT_COMPILE
    {
        struct Big {std::size_t a=12; std::size_t b=13; } big;
        Slot<int> slot = toolbox::bind([big](int y) { 
            BOOST_TEST(big.a==12);
            BOOST_TEST(big.b==13);
        });
        slot(6);
    }
    #endif
    {
        struct Clazz {
            void op() {
                Slot<int> slot = toolbox::bind([this](int y) {
                    BOOST_TEST(z==12);
                    BOOST_TEST(y==13);
                    z=y;
                });
                slot(13);
                BOOST_TEST(z==13);
            }
            int z=12;
        };
    }

}

BOOST_AUTO_TEST_SUITE_END()
