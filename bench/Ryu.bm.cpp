// The Reactive C++ Toolbox.
// Copyright (C) 2021 Reactive Markets Limited
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

#include <toolbox/util/Ryu.hpp>
#include <toolbox/util/Utility.hpp>

#include <toolbox/bm.hpp>

#include <boost/spirit/include/karma.hpp>

#include <cstdio>

TOOLBOX_BENCHMARK_MAIN

using namespace std;
using namespace toolbox;

namespace karma = boost::spirit::karma;

namespace {
constexpr double Qty = 1e6;
constexpr double Price = 1.234567;

struct FixedPolicy : boost::spirit::karma::real_policies<double> {
    static int floatfield(double) noexcept { return fmtflags::fixed; }
    static int precision(double d)
    {
        // See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Number/MAX_SAFE_INTEGER.
        constexpr double MaxSafeInt = (1L << 53L) - 1L;
        d = std::abs(d);
        if (d > MaxSafeInt) {
            throw std::out_of_range{"number out of range"};
        }
        return std::min(16 - dec_digits(d), 9);
    }
};

const karma::real_generator<double, FixedPolicy> Fixed;

TOOLBOX_BENCHMARK(dtos_buf_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtos(buf, Qty);
        }
    }
}

TOOLBOX_BENCHMARK(dtos_buf_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtos(buf, Price);
        }
    }
}

TOOLBOX_BENCHMARK(dtos_tls_qty)
{
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtos(Qty);
        }
    }
}

TOOLBOX_BENCHMARK(dtos_tls_price)
{
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtos(Price);
        }
    }
}

TOOLBOX_BENCHMARK(dtofixed_buf_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtofixed(buf, Qty);
        }
    }
}

TOOLBOX_BENCHMARK(dtofixed_buf_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtofixed(buf, Price);
        }
    }
}

TOOLBOX_BENCHMARK(dtofixed_tls_qty)
{
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtofixed(Qty);
        }
    }
}

TOOLBOX_BENCHMARK(dtofixed_tls_price)
{
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtofixed(Price);
        }
    }
}

TOOLBOX_BENCHMARK(karma_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            double d = Qty;
            bm::do_not_optimise(d);
            char* it = buf;
            karma::generate(it, Fixed, d);
        }
    }
}

TOOLBOX_BENCHMARK(karma_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            char* it = buf;
            karma::generate(it, Fixed, Price);
        }
    }
}

TOOLBOX_BENCHMARK(sprintf_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            ::sprintf(buf, "%f", Qty);
        }
    }
}

TOOLBOX_BENCHMARK(sprintf_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            ::sprintf(buf, "%f", Price);
        }
    }
}

TOOLBOX_BENCHMARK(strfromd_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            ::strfromd(buf, sizeof(buf), "%f", Qty);
        }
    }
}

TOOLBOX_BENCHMARK(strfromd_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            ::strfromd(buf, sizeof(buf), "%f", Price);
        }
    }
}

} // namespace
