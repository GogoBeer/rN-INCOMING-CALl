// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <streams.h>
#include <test/util/script.h>
#include <version.h>

#include <test/fuzz/fuzz.h>

void initialize_script_flags()
{
    static const ECCVerifyHandle verify_handle;
}

FUZZ_TARGET_INIT(script_flags, initialize_script_flags)
{
    CDataStream ds(buffer, SER_NETWORK, INIT_PROTO_VERSION);
    try {
        int nVersion;
        ds >> nVersion;
        ds.SetVersion(nVersion);
    } catch (const std::ios_base::failure&) {
        return;
    }

    try {
        const CTransaction tx(deserialize, ds);

        unsigned int verify_flags;
        ds >> verify_flags;

        if (!IsValidFlagCombination(verify_flags)) return;

        unsigned int fuzzed_flags;
        ds >> fuzzed_flags;

        std::ve