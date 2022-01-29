// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

FUZZ_TARGET(str_printf)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    const std::string format_string = fuzzed_data_provider.ConsumeRandomLengthString(64);
    const bilingual_str bilingual_string{format_string, format_string};

    const int digits_in_format_specifier = std::count_if(format_string.begin(), format_string.end(), IsDigit);

    // Avoid triggering the following crash bug:
    // * strprintf("%987654321000000:", 1);
    //
    // Avoid triggering the following OOM bug:
    // * strprintf("%.222222200000000$", 1.1);
    //
    // Upstream bug report: https://github.com/c42f/tinyformat/issues/70
    if (format_string.find('%') != std::string::npos && digits_in_format_specifier >= 7) {
        return;
    }

    // Avoid triggering the following crash bug:
    // * strprintf("%1$*1$*", -11111111);
    //
    // Upstream bug report: https://github.com/c42f/tinyformat/issues/70
    if (format_string.find('%') != std::string::npos && format_string.find('$') != std::string::npos && format_string.find('*') != std::string::npos && digits_in_format_specifier > 0) {
        return;
    }

    // Avoid triggering the following crash bug:
    // * strprintf("%.1s", (char*)nullptr);
    //
    // (void)strprintf(format_string, (char*)nullptr);
    //
    // Upstream bug report: https://github.com/c42f/tinyformat/issues/70

    try {
        CallOneOf(
            fuzzed_data_provider,
            [&] {
                (void)strprintf(format_string, fuzzed_data_provider.ConsumeRandomLengthString(32));
                (void)tinyformat::format(bilingual_string, fuzzed_data_provider.ConsumeRandomLengthString(32));
            },
            [&] {
                (void)strprintf(format_string, fuzzed_data_provider.ConsumeRandomLengthString(32).c_str());
                (void)tinyformat::format(bilingual_string, fuzzed_data_provider.ConsumeRandomLengthString(32).c_str());
            },
            [&] {
                (void)strprintf(format_string, fuzzed_data_provider.ConsumeIntegral<signed char>());
                (void)tinyformat::format(bilingual_string, fuzzed_data_provider.ConsumeIntegral<signed char>());
            },
            [&] {
                (void)strprintf(format_string, fuzzed_data_provider.ConsumeIntegral<unsigned char>());
                (void)tinyformat::format(bilingual_string, fuzzed_data_provider.ConsumeIntegral<unsigned char>());
            },
            [&] {
                (void)strprintf(format_string, fuzzed_data_provider.ConsumeIntegral<char>());
                (void)tinyformat::format(bilingual_string, fuzzed_data_provider.ConsumeIntegral<char>());
            },
            [&] {
                (void)strprintf(format_string, fuzzed_data_provider.ConsumeBool());
                (void)tinyformat::format(bilingual_string, fuzzed_data_provider.ConsumeBool());
            });
    } catch (const tinyformat::format_error&) {
    }

    if (format_string.find('%') != std::string::npos && format_string.find('c') != std::string::npos) {
        