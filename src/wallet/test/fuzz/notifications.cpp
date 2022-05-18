// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <test/util/setup_common.h>
#include <util/translation.h>
#include <wallet/context.h>
#include <wallet/receive.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <wallet/walletutil.h>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {
const TestingSetup* g_setup;

void initialize_setup()
{
    static const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    g_setup = testing_setup.get();
}

/**
 * Wraps a descriptor wallet for fuzzing. The constructor writes the sqlite db
 * to disk, the destructor deletes it.
 */
struct FuzzedWallet {
    ArgsManager args;
    WalletContext context;
    std::shared_ptr<CWallet> wallet;
    FuzzedWallet(const std::string& name)
    {
        context.args = &args;
        context.chain = g_setup->m_node.chain.get();

        DatabaseOptions options;
        options.require_create = true;
        options.create_flags = WALLET_FLAG_DESCRIPTORS;
        const std::optional<bool> load_on_start;
        gArgs.ForceSetArg("-keypool", "0"); // Avoid timeout in TopUp()

        DatabaseStatus status;
        bilingual_str error;
        std::vector<bilingual_str> warnings;
        wallet = CreateWallet(context, name, load_on_start, options, status, error, warnings);
        assert(wallet);
        assert(error.empty());
        assert(warnings.empty());
        assert(wallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS));
    }
    ~FuzzedWallet()
    {
        const auto name{wallet->GetName()};
        std::vector<bilingual_str> warnings;
        std::optional<bool> load_on_start;
        assert(RemoveWallet(context, wallet, load_on_start, warnings));
        assert(warnings.empty());
        UnloadWallet(std::move(wallet));
        fs::remove_all(GetWalletDir() / name);
    }
    CScript GetScriptPubKey(FuzzedDataProvider& fuzzed_data_provider)
    {
        auto type{fuzzed_data_provider.PickValueInArray(OUTPUT_TYPES)};
        CTxDestination dest;
        bilingual_str error;
        if (fuzzed_data_provider.ConsumeBool()) {
            assert(wallet->GetNewDestination(type, "", dest, error));
        } else {
            assert(wallet->GetNewChangeDestination(type, dest, error));
        }
        assert(error.empty());
        return GetScriptForDestination(dest);
    }
};

FUZZ_TARGET_INIT(wallet_notifications, initialize_setup)
{
    FuzzedDataProvider fuzzed_data_provider{buffer.data(), buffer.size()};
    // The total amount, to be distributed to the wallets a and b in txs
    // without fee. Thus, the balance of the wallets should always equal the
    // total amount.
    const auto total_amount{ConsumeMoney