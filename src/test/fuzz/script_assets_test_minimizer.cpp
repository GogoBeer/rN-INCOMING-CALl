// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/fuzz/fuzz.h>

#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <streams.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <boost/algorithm/string.hpp>
#include <cstdint>
#include <string>
#include <vector>

// This fuzz "test" can be used to minimize test cases for script_assets_test in
// src/test/script_tests.cpp. While it written as a fuzz test, and can be used as such,
// fuzzing the inputs is unlikely to construct useful test cases.
//
// Instead, it is primarily intended to be run on a test set that was generated
// externally, for example using test/functional/feature_taproot.py's --dumptests mode.
// The minimized set can then be concatenated together, surrounded by '[' and ']',
// and used as the script_assets_test.json input to the script_assets_test unit test:
//
// (normal build)
// $ mkdir dump
// $ for N in $(seq 1 10); do TEST_DUMP_DIR=dump test/functional/feature_taproot.py --dumptests; done
// $ ...
//
// (libFuzzer build)
// $ mkdir dump-min
// $ FUZZ=script_assets_test_minimizer ./src/test/fuzz/fuzz -merge=1 -use_value_profile=1 dump-min/ dump/
// $ (echo -en '[\n'; cat dump-min/* | head -c -2; echo -en '\n]') >script_assets_test.json

namespace {

std::vector<unsigned char> CheckedParseHex(const std::string& str)
{
    if (str.size() && !IsHex(str)) throw std::runtime_error("Non-hex input '" + str + "'");
    return ParseHex(str);
}

CScript ScriptFromHex(const std::string& str)
{
    std::vector<unsigned char> data = CheckedParseHex(str);
    return CScript(data.begin(), data.end());
}

CMutableTransaction TxFromHex(const std::string& str)
{
    CMutableTransaction tx;
    try {
        SpanReader{SER_DISK, SERIALIZE_TRANSACTION_NO_WITNESS, CheckedParseHex(str)} >> tx;
    } catch (const std::ios_base::failure&) {
        throw std::runtime_error("Tx deserialization failure");
    }
    return tx;
}

std::vector<CTxOut> TxOutsFromJSON(const UniValue& univalue)
{
    if (!univalue.isArray()) throw std::runtime_error("Prevouts must be array");
    std::vector<CTxOut> prevouts;
    for (size_t i = 0; i < univalue.size(); ++i) {
        CTxOut txout;
        try {
            SpanReader{SER_DISK, 0, CheckedParseHex(univalue[i].get_str())} >> txout;
        } catch (const std::ios_base::failure&) {
            throw std::runtime_error("Prevout inva