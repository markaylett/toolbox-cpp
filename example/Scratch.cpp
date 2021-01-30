// The Reactive C++ Toolbox.
// Copyright (C) 2013-2019 Swirly Cloud Limited
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

#include <toolbox/util.hpp>

#include <iostream>

#include <boost/spirit/include/karma.hpp>
namespace karma = boost::spirit::karma;

using namespace std;
using namespace toolbox;

namespace {
constexpr double Qty = 1e6;
constexpr double Price = 123.456789;

inline std::to_chars_result to_chars(char* begin, char* end, bool sign, std::int64_t mantissa, int exponent) noexcept
{
    char* it = begin;
    do {
        if (it >= end) {
            return {begin, errc::value_too_large};
        }
        const auto digit = mantissa % 10;
        mantissa /= 10;
        if (digit != 0 || it != begin) {
            *it++ = '0' + digit;
        }
        if (++exponent == 0) {
            if (it >= end) {
                return {begin, errc::value_too_large};
            }
            *it++ = '.';
        }
    } while (mantissa > 0 || exponent < 1);

    if (sign) {
        if (it >= end) {
            return {begin, errc::value_too_large};
        }
        *it++ = '-';
    }
    // Reverse the digits in-place.
    std::reverse(begin, it);
    return {it, errc{}};
}

inline std::to_chars_result to_chars(char* begin, char* end, double value) noexcept
{
    if (std::trunc(value) == value) {
        return std::to_chars(begin, end, static_cast<std::int64_t>(value));
    }
    bool sign{false};
    if (std::signbit(value)) {
        sign = true;
        value = std::abs(value);
    }
    int exponent{std::max(dec_digits(value) - 16, -9)};
    std::int64_t mantissa = value * std::pow(10, -exponent);
    if (exponent < -6 && mantissa % 1000000 == 0) {
        mantissa /= 1000000;
        exponent += 6;
    }
    if (exponent < -3 && mantissa % 1000 == 0) {
        mantissa /= 1000;
        exponent += 3;
    }
    return to_chars(begin, end, sign, mantissa, exponent);
}

struct FixedPolicy : boost::spirit::karma::real_policies<double> {
    static int floatfield(double) noexcept { return fmtflags::fixed; }
    static int precision(double d)
    {
        // #define DOUBLE_MANTISSA_BITS 52
        // Between 2^52=4,503,599,627,370,496 and 2^53=9,007,199,254,740,992 the representable numbers are exactly the integers.
        // See also https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Number/MAX_SAFE_INTEGER.
        constexpr double MaxSafeInt = (1L << 53L) - 1L;
        d = std::abs(d);
        if (d > MaxSafeInt) {
            throw std::out_of_range{"number out of range"};
        }
        if (d >= 1e12) {
            return 0;
        }
        return 12 - dec_digits(d);
    }
};

#if 0
double d = 0.0001234567;

        namespace karma = boost::spirit::karma;
        const karma::real_generator<double, FixedPolicy> Fixed;

        //std::string s;
        //std::back_insert_iterator<std::string> sink(s);

        char buf[64];
        char* begin, *end;
        begin = end = buf;
        karma::generate(end, Fixed, d);
        cout << string_view{begin, size_t(end - begin)} << endl;
#endif

} // namespace

int main(int argc, char* argv[])
{
    int ret = 1;
    try {

        char buf[64];
        char* begin = buf;
        double d = 1e6;
        //auto [end, ec] = to_chars(begin, begin + 64, -0.000123);
        auto [end, ec] = to_chars(begin, begin + 64, d);
        size_t size = end - begin;
        cout << size << endl;
        cout << string_view{begin, size} << endl;

        ret = 0;

    } catch (const std::exception& e) {
        cout << "exception on main thread: " << e.what();
    }
    return ret;
}
