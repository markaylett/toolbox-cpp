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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <dragonbox/dragonbox_to_chars.h>
#pragma GCC diagnostic pop
#include <fast_float/fast_float.h>

#include <charconv>
#include <random>

TOOLBOX_BENCHMARK_MAIN

using namespace std;
using namespace toolbox;

namespace karma = boost::spirit::karma;

namespace {
constexpr double Qty = 1e6;
constexpr double Price = 1.234567;

#if 0
char* itoa(std::int64_t number, char* buffer, char* end)
{
    // Write out the digits in reverse order.
    bool sign(false);
    if (number < 0) {
        sign = true;
        number = -number;
    }

    char* b = buffer;
    do {
        if (b >= end) {
            throw_range_error();
        }
        *b++ = '0' + (number % 10);
        number /= 10;
    } while(number);

    if (sign) {
        if (b >= end) {
            throw_range_error();
        }
        *b++ = '-';
    }

    // Reverse the digits in-place.
    std::reverse(buffer, b);
    return b;
}
#endif

to_chars_result to_chars(char* buffer, char* end, bool sign, int64_t mantissa, int exponent) noexcept
{
    char* b = buffer;
    do {
        auto digit = mantissa % 10;
        mantissa /= 10;
        if (digit != 0 || b != buffer) {
            if (b >= end) {
                return {buffer, errc::value_too_large};
            }
            *b++ = '0' + digit;
        }
        if (++exponent == 0) {
            if (b >= end) {
                return {buffer, errc::value_too_large};
            }
            *b++ = '.';
        }
    } while (mantissa > 0 || exponent < 1);

    if (sign) {
        if (b >= end) {
            return {buffer, errc::value_too_large};
        }
        *b++ = '-';
    }
    // Reverse the digits in-place.
    std::reverse(buffer, b);
    return {b, errc::value_too_large};
}

to_chars_result to_chars(char* buffer, char* end, double number) noexcept
{
    if (std::trunc(number) == number) {
        return std::to_chars(buffer, end, static_cast<int64_t>(number));
    }
    bool sign{false};
    if (signbit(number)) {
        sign = true;
        number = abs(number);
    }
    int exponent = std::max(dec_digits(number) - 16, -9);
    int64_t mantissa = number * pow(10, -exponent);
    if (exponent < -6 && mantissa % 1000000 == 0) {
        mantissa /= 1000000;
        exponent += 6;
    }
    if (exponent < -3 && mantissa % 1000 == 0) {
        mantissa /= 1000;
        exponent += 3;
    }
    return to_chars(buffer, end, sign, mantissa, exponent);
}

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

TOOLBOX_BENCHMARK(to_chars_int)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            int64_t i = Qty;
            bm::do_not_optimise(i);
            std::to_chars(buf, buf + 64, i);
        }
    }
}

TOOLBOX_BENCHMARK(dtoa_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(10000)) {
            int64_t i = Qty;
            bm::do_not_optimise(i);
            char* begin = buf;
            char* end = begin + 64;
            to_chars(begin, end, i);
        }
    }
}

TOOLBOX_BENCHMARK(dtoa_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(10000)) {
            char* begin = buf;
            char* end = begin + 64;
            to_chars(begin, end, Price);
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

TOOLBOX_BENCHMARK(dtofixed_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtofixed(buf, Qty);
        }
    }
}

TOOLBOX_BENCHMARK(dtofixed_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::dtofixed(buf, Price);
        }
    }
}

#if 0
TOOLBOX_BENCHMARK(to_chars_dragonbox_qty)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            jkj::dragonbox::to_chars(Qty, buf);
        }
    }
}

TOOLBOX_BENCHMARK(to_chars_dragonbox_price)
{
    char buf[64];
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            jkj::dragonbox::to_chars(Price, buf);
        }
    }
}

TOOLBOX_BENCHMARK(from_chars)
{
    constexpr auto sv = "1.234567"sv;
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            double result;
            std::from_chars(sv.data(), sv.data() + sv.size(), result);
        }
    }
}

TOOLBOX_BENCHMARK(from_chars_fast_float)
{
    constexpr auto sv = "1.234567"sv;
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            double result;
            fast_float::from_chars(sv.data(), sv.data() + sv.size(), result, fast_float::chars_format::fixed);
        }
    }
}

TOOLBOX_BENCHMARK(stod)
{
    while (ctx) {
        for (auto _ : ctx.range(1000)) {
            util::stod("1.234567"sv);
        }
    }
}

TOOLBOX_BENCHMARK(modf)
{
    std::random_device rd;
    std::mt19937 e2(rd());
    std::uniform_real_distribution<> dist(0, 1);
    auto d = dist(e2);

    while (ctx) {
        for (auto _ : ctx.range(10000)) {

            const int64_t i = d;
            const int id = dec_digits(i);
            bm::do_not_optimise(id);
        }
    }
}
#endif
} // namespace
