// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <script/script.h>
#include <util/strencodings.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(script_parse_tests)
BOOST_AUTO_TEST_CASE(parse_script)
{
    const std::vector<std::pair<std::string,std::string>> IN_OUT{
        // {IN: script string , OUT: hex string }
        {"", ""},
        {"0", "00"},
        {"1", "51"},
        {"2", "52"},
        {"3", "53"},
        {"4", "54"},
        {"5", "55"},
        {"6", "56"},
        {"7", "57"},
        {"8", "58"},
        {