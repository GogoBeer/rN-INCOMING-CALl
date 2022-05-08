
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <clientversion.h>
#include <core_io.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <merkleblock.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/standard.h>
#include <sync.h>
#include <util/bip32.h>
#include <util/system.h>
#include <util/time.h>
#include <util/translation.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>

#include <stdint.h>
#include <tuple>

#include <boost/algorithm/string.hpp>

#include <univalue.h>



using interfaces::FoundBlock;

std::string static EncodeDumpString(const std::string &str) {
    std::stringstream ret;
    for (const unsigned char c : str) {
        if (c <= 32 || c >= 128 || c == '%') {
            ret << '%' << HexStr({&c, 1});
        } else {
            ret << c;
        }
    }
    return ret.str();
}

static std::string DecodeDumpString(const std::string &str) {
    std::stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++) {
        unsigned char c = str[pos];
        if (c == '%' && pos+2 < str.length()) {
            c = (((str[pos+1]>>6)*9+((str[pos+1]-'0')&15)) << 4) |
                ((str[pos+2]>>6)*9+((str[pos+2]-'0')&15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

static bool GetWalletAddressesForKey(const LegacyScriptPubKeyMan* spk_man, const CWallet& wallet, const CKeyID& keyid, std::string& strAddr, std::string& strLabel) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    bool fLabelFound = false;
    CKey key;
    spk_man->GetKey(keyid, key);
    for (const auto& dest : GetAllDestinationsForKey(key.GetPubKey())) {
        const auto* address_book_entry = wallet.FindAddressBookEntry(dest);
        if (address_book_entry) {
            if (!strAddr.empty()) {
                strAddr += ",";
            }
            strAddr += EncodeDestination(dest);
            strLabel = EncodeDumpString(address_book_entry->GetLabel());
            fLabelFound = true;
        }
    }
    if (!fLabelFound) {
        strAddr = EncodeDestination(GetDestinationForKey(key.GetPubKey(), wallet.m_default_address_type));
    }
    return fLabelFound;
}

static const int64_t TIMESTAMP_MIN = 0;

static void RescanWallet(CWallet& wallet, const WalletRescanReserver& reserver, int64_t time_begin = TIMESTAMP_MIN, bool update = true)
{
    int64_t scanned_time = wallet.RescanFromTime(time_begin, reserver, update);
    if (wallet.IsAbortingRescan()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted by user.");
    } else if (scanned_time > time_begin) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan was unable to fully rescan the blockchain. Some transactions may be missing.");
    }
}

RPCHelpMan importprivkey()
{
    return RPCHelpMan{"importprivkey",
                "\nAdds a private key (as returned by dumpprivkey) to your wallet. Requires a new wallet backup.\n"
                "Hint: use importmulti to import more than one private key.\n"
            "\nNote: This call can take over an hour to complete if rescan is true, during that time, other rpc calls\n"
            "may report that the imported key exists but related transactions are still missing, leading to temporarily incorrect/bogus balances and unspent outputs until rescan completes.\n"
            "Note: Use \"getwalletinfo\" to query the scanning progress.\n",
                {
                    {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The private key (see dumpprivkey)"},
                    {"label", RPCArg::Type::STR, RPCArg::DefaultHint{"current label if address exists, otherwise \"\""}, "An optional label"},
                    {"rescan", RPCArg::Type::BOOL, RPCArg::Default{true}, "Rescan the wallet for transactions"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
            "\nDump a private key\n"
            + HelpExampleCli("dumpprivkey", "\"myaddress\"") +
            "\nImport the private key with rescan\n"
            + HelpExampleCli("importprivkey", "\"mykey\"") +
            "\nImport using a label and without rescan\n"
            + HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") +
            "\nImport using default blank label and without rescan\n"
            + HelpExampleCli("importprivkey", "\"mykey\" \"\" false") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return NullUniValue;

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot import private keys to a wallet with private keys disabled");
    }

    EnsureLegacyScriptPubKeyMan(*pwallet, true);

    WalletRescanReserver reserver(*pwallet);
    bool fRescan = true;
    {
        LOCK(pwallet->cs_wallet);

        EnsureWalletIsUnlocked(*pwallet);

        std::string strSecret = request.params[0].get_str();
        std::string strLabel = "";
        if (!request.params[1].isNull())
            strLabel = request.params[1].get_str();

        // Whether to perform rescan after import
        if (!request.params[2].isNull())
            fRescan = request.params[2].get_bool();

        if (fRescan && pwallet->chain().havePruned()) {
            // Exit early and print an error.
            // If a block is pruned after this check, we will import the key(s),
            // but fail the rescan with a generic error.
            throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled when blocks are pruned");
        }

        if (fRescan && !reserver.reserve()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
        }

        CKey key = DecodeSecret(strSecret);
        if (!key.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

        CPubKey pubkey = key.GetPubKey();
        CHECK_NONFATAL(key.VerifyPubKey(pubkey));
        CKeyID vchAddress = pubkey.GetID();
        {
            pwallet->MarkDirty();

            // We don't know which corresponding address will be used;
            // label all new addresses, and label existing addresses if a
            // label was passed.
            for (const auto& dest : GetAllDestinationsForKey(pubkey)) {
                if (!request.params[1].isNull() || !pwallet->FindAddressBookEntry(dest)) {
                    pwallet->SetAddressBook(dest, strLabel, "receive");
                }
            }

            // Use timestamp of 1 to scan the whole chain
            if (!pwallet->ImportPrivKeys({{vchAddress, key}}, 1)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");
            }

            // Add the wpkh script for this key if possible
            if (pubkey.IsCompressed()) {
                pwallet->ImportScripts({GetScriptForDestination(WitnessV0KeyHash(vchAddress))}, 0 /* timestamp */);
            }
        }
    }
    if (fRescan) {
        RescanWallet(*pwallet, reserver);
    }

    return NullUniValue;
},
    };
}

RPCHelpMan importaddress()
{
    return RPCHelpMan{"importaddress",
                "\nAdds an address or script (in hex) that can be watched as if it were in your wallet but cannot be used to spend. Requires a new wallet backup.\n"
            "\nNote: This call can take over an hour to complete if rescan is true, during that time, other rpc calls\n"
            "may report that the imported address exists but related transactions are still missing, leading to temporarily incorrect/bogus balances and unspent outputs until rescan completes.\n"
            "If you have the full public key, you should call importpubkey instead of this.\n"
            "Hint: use importmulti to import more than one address.\n"
            "\nNote: If you import a non-standard raw script in hex form, outputs sending to it will be treated\n"
            "as change, and not show up in many RPCs.\n"
            "Note: Use \"getwalletinfo\" to query the scanning progress.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Bitcoin address (or hex-encoded script)"},
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "An optional label"},
                    {"rescan", RPCArg::Type::BOOL, RPCArg::Default{true}, "Rescan the wallet for transactions"},
                    {"p2sh", RPCArg::Type::BOOL, RPCArg::Default{false}, "Add the P2SH version of the script as well"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
            "\nImport an address with rescan\n"
            + HelpExampleCli("importaddress", "\"myaddress\"") +
            "\nImport using a label without rescan\n"
            + HelpExampleCli("importaddress", "\"myaddress\" \"testing\" false") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("importaddress", "\"myaddress\", \"testing\", false")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return NullUniValue;

    EnsureLegacyScriptPubKeyMan(*pwallet, true);

    std::string strLabel;
    if (!request.params[1].isNull())
        strLabel = request.params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (!request.params[2].isNull())
        fRescan = request.params[2].get_bool();

    if (fRescan && pwallet->chain().havePruned()) {
        // Exit early and print an error.
        // If a block is pruned after this check, we will import the key(s),
        // but fail the rescan with a generic error.
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled when blocks are pruned");
    }

    WalletRescanReserver reserver(*pwallet);
    if (fRescan && !reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    // Whether to import a p2sh version, too
    bool fP2SH = false;
    if (!request.params[3].isNull())
        fP2SH = request.params[3].get_bool();

    {
        LOCK(pwallet->cs_wallet);

        CTxDestination dest = DecodeDestination(request.params[0].get_str());
        if (IsValidDestination(dest)) {
            if (fP2SH) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Cannot use the p2sh flag with an address - use a script instead");
            }
            if (OutputTypeFromDestination(dest) == OutputType::BECH32M) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Bech32m addresses cannot be imported into legacy wallets");
            }

            pwallet->MarkDirty();

            pwallet->ImportScriptPubKeys(strLabel, {GetScriptForDestination(dest)}, false /* have_solving_data */, true /* apply_label */, 1 /* timestamp */);
        } else if (IsHex(request.params[0].get_str())) {
            std::vector<unsigned char> data(ParseHex(request.params[0].get_str()));
            CScript redeem_script(data.begin(), data.end());

            std::set<CScript> scripts = {redeem_script};
            pwallet->ImportScripts(scripts, 0 /* timestamp */);

            if (fP2SH) {
                scripts.insert(GetScriptForDestination(ScriptHash(redeem_script)));
            }

            pwallet->ImportScriptPubKeys(strLabel, scripts, false /* have_solving_data */, true /* apply_label */, 1 /* timestamp */);
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address or script");
        }
    }
    if (fRescan)
    {
        RescanWallet(*pwallet, reserver);
        {
            LOCK(pwallet->cs_wallet);
            pwallet->ReacceptWalletTransactions();
        }
    }

    return NullUniValue;
},
    };
}

RPCHelpMan importprunedfunds()
{
    return RPCHelpMan{"importprunedfunds",
                "\nImports funds without rescan. Corresponding address or script must previously be included in wallet. Aimed towards pruned wallets. The end-user is responsible to import additional transactions that subsequently spend the imported outputs or rescan after the point in the blockchain the transaction is included.\n",
                {
                    {"rawtransaction", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "A raw transaction in hex funding an already-existing address in wallet"},
                    {"txoutproof", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hex output from gettxoutproof that contains the transaction"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return NullUniValue;

    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed. Make sure the tx has at least one input.");
    }
    uint256 hashTx = tx.GetHash();

    CDataStream ssMB(ParseHexV(request.params[1], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    //Search partial merkle tree in proof for our transaction and index in valid block
    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Something wrong with merkleblock");
    }

    LOCK(pwallet->cs_wallet);
    int height;
    if (!pwallet->chain().findAncestorByHash(pwallet->GetLastBlockHash(), merkleBlock.header.GetHash(), FoundBlock().height(height))) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");
    }

    std::vector<uint256>::const_iterator it;
    if ((it = std::find(vMatch.begin(), vMatch.end(), hashTx)) == vMatch.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction given doesn't exist in proof");
    }

    unsigned int txnIndex = vIndex[it - vMatch.begin()];

    CTransactionRef tx_ref = MakeTransactionRef(tx);
    if (pwallet->IsMine(*tx_ref)) {
        pwallet->AddToWallet(std::move(tx_ref), TxStateConfirmed{merkleBlock.header.GetHash(), height, static_cast<int>(txnIndex)});
        return NullUniValue;
    }

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No addresses in wallet correspond to included transaction");
},
    };
}

RPCHelpMan removeprunedfunds()
{
    return RPCHelpMan{"removeprunedfunds",
                "\nDeletes the specified transaction from the wallet. Meant for use with pruned wallets and as a companion to importprunedfunds. This will affect wallet balances.\n",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hex-encoded id of the transaction you are deleting"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("removeprunedfunds", "\"a8d0c0184dde994a09ec054286f1ce581bebf46446a512166eae7628734ea0a5\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("removeprunedfunds", "\"a8d0c0184dde994a09ec054286f1ce581bebf46446a512166eae7628734ea0a5\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return NullUniValue;

    LOCK(pwallet->cs_wallet);

    uint256 hash(ParseHashV(request.params[0], "txid"));
    std::vector<uint256> vHash;
    vHash.push_back(hash);
    std::vector<uint256> vHashOut;

    if (pwallet->ZapSelectTx(vHash, vHashOut) != DBErrors::LOAD_OK) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Could not properly delete the transaction.");
    }

    if(vHashOut.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Transaction does not exist in wallet.");
    }

    return NullUniValue;
},
    };
}

RPCHelpMan importpubkey()
{
    return RPCHelpMan{"importpubkey",
                "\nAdds a public key (in hex) that can be watched as if it were in your wallet but cannot be used to spend. Requires a new wallet backup.\n"
                "Hint: use importmulti to import more than one public key.\n"
            "\nNote: This call can take over an hour to complete if rescan is true, during that time, other rpc calls\n"
            "may report that the imported pubkey exists but related transactions are still missing, leading to temporarily incorrect/bogus balances and unspent outputs until rescan completes.\n"
            "Note: Use \"getwalletinfo\" to query the scanning progress.\n",
                {
                    {"pubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The hex-encoded public key"},
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "An optional label"},
                    {"rescan", RPCArg::Type::BOOL, RPCArg::Default{true}, "Rescan the wallet for transactions"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
            "\nImport a public key with rescan\n"
            + HelpExampleCli("importpubkey", "\"mypubkey\"") +
            "\nImport using a label without rescan\n"
            + HelpExampleCli("importpubkey", "\"mypubkey\" \"testing\" false") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("importpubkey", "\"mypubkey\", \"testing\", false")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return NullUniValue;

    EnsureLegacyScriptPubKeyMan(*pwallet, true);

    std::string strLabel;
    if (!request.params[1].isNull())
        strLabel = request.params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (!request.params[2].isNull())
        fRescan = request.params[2].get_bool();

    if (fRescan && pwallet->chain().havePruned()) {
        // Exit early and print an error.
        // If a block is pruned after this check, we will import the key(s),
        // but fail the rescan with a generic error.
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled when blocks are pruned");
    }

    WalletRescanReserver reserver(*pwallet);
    if (fRescan && !reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    if (!IsHex(request.params[0].get_str()))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey must be a hex string");
    std::vector<unsigned char> data(ParseHex(request.params[0].get_str()));
    CPubKey pubKey(data);
    if (!pubKey.IsFullyValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey is not a valid public key");

    {
        LOCK(pwallet->cs_wallet);

        std::set<CScript> script_pub_keys;
        for (const auto& dest : GetAllDestinationsForKey(pubKey)) {
            script_pub_keys.insert(GetScriptForDestination(dest));
        }

        pwallet->MarkDirty();

        pwallet->ImportScriptPubKeys(strLabel, script_pub_keys, true /* have_solving_data */, true /* apply_label */, 1 /* timestamp */);

        pwallet->ImportPubKeys({pubKey.GetID()}, {{pubKey.GetID(), pubKey}} , {} /* key_origins */, false /* add_keypool */, false /* internal */, 1 /* timestamp */);
    }
    if (fRescan)
    {
        RescanWallet(*pwallet, reserver);
        {
            LOCK(pwallet->cs_wallet);
            pwallet->ReacceptWalletTransactions();
        }
    }

    return NullUniValue;
},
    };
}


RPCHelpMan importwallet()
{
    return RPCHelpMan{"importwallet",
                "\nImports keys from a wallet dump file (see dumpwallet). Requires a new wallet backup to include imported keys.\n"
                "Note: Use \"getwalletinfo\" to query the scanning progress.\n",
                {
                    {"filename", RPCArg::Type::STR, RPCArg::Optional::NO, "The wallet file"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
            "\nDump the wallet\n"
            + HelpExampleCli("dumpwallet", "\"test\"") +
            "\nImport the wallet\n"
            + HelpExampleCli("importwallet", "\"test\"") +
            "\nImport using the json rpc call\n"
            + HelpExampleRpc("importwallet", "\"test\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return NullUniValue;

    EnsureLegacyScriptPubKeyMan(*pwallet, true);

    if (pwallet->chain().havePruned()) {
        // Exit early and print an error.
        // If a block is pruned after this check, we will import the key(s),
        // but fail the rescan with a generic error.
        throw JSONRPCError(RPC_WALLET_ERROR, "Importing wallets is disabled when blocks are pruned");
    }

    WalletRescanReserver reserver(*pwallet);
    if (!reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    int64_t nTimeBegin = 0;
    bool fGood = true;
    {
        LOCK(pwallet->cs_wallet);

        EnsureWalletIsUnlocked(*pwallet);

        fsbridge::ifstream file;
        file.open(fs::u8path(request.params[0].get_str()), std::ios::in | std::ios::ate);
        if (!file.is_open()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");
        }
        CHECK_NONFATAL(pwallet->chain().findBlock(pwallet->GetLastBlockHash(), FoundBlock().time(nTimeBegin)));

        int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
        file.seekg(0, file.beg);

        // Use uiInterface.ShowProgress instead of pwallet.ShowProgress because pwallet.ShowProgress has a cancel button tied to AbortRescan which
        // we don't want for this progress bar showing the import progress. uiInterface.ShowProgress does not have a cancel button.
        pwallet->chain().showProgress(strprintf("%s " + _("Importing…").translated, pwallet->GetDisplayName()), 0, false); // show progress dialog in GUI
        std::vector<std::tuple<CKey, int64_t, bool, std::string>> keys;
        std::vector<std::pair<CScript, int64_t>> scripts;
        while (file.good()) {
            pwallet->chain().showProgress("", std::max(1, std::min(50, (int)(((double)file.tellg() / (double)nFilesize) * 100))), false);
            std::string line;
            std::getline(file, line);
            if (line.empty() || line[0] == '#')
                continue;

            std::vector<std::string> vstr;
            boost::split(vstr, line, boost::is_any_of(" "));
            if (vstr.size() < 2)
                continue;
            CKey key = DecodeSecret(vstr[0]);
            if (key.IsValid()) {
                int64_t nTime = ParseISO8601DateTime(vstr[1]);
                std::string strLabel;
                bool fLabel = true;
                for (unsigned int nStr = 2; nStr < vstr.size(); nStr++) {
                    if (vstr[nStr].front() == '#')
                        break;
                    if (vstr[nStr] == "change=1")
                        fLabel = false;
                    if (vstr[nStr] == "reserve=1")
                        fLabel = false;
                    if (vstr[nStr].substr(0,6) == "label=") {
                        strLabel = DecodeDumpString(vstr[nStr].substr(6));
                        fLabel = true;
                    }
                }
                keys.push_back(std::make_tuple(key, nTime, fLabel, strLabel));
            } else if(IsHex(vstr[0])) {
                std::vector<unsigned char> vData(ParseHex(vstr[0]));
                CScript script = CScript(vData.begin(), vData.end());
                int64_t birth_time = ParseISO8601DateTime(vstr[1]);
                scripts.push_back(std::pair<CScript, int64_t>(script, birth_time));
            }
        }
        file.close();
        // We now know whether we are importing private keys, so we can error if private keys are disabled
        if (keys.size() > 0 && pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            pwallet->chain().showProgress("", 100, false); // hide progress dialog in GUI
            throw JSONRPCError(RPC_WALLET_ERROR, "Importing wallets is disabled when private keys are disabled");
        }
        double total = (double)(keys.size() + scripts.size());
        double progress = 0;
        for (const auto& key_tuple : keys) {
            pwallet->chain().showProgress("", std::max(50, std::min(75, (int)((progress / total) * 100) + 50)), false);
            const CKey& key = std::get<0>(key_tuple);
            int64_t time = std::get<1>(key_tuple);
            bool has_label = std::get<2>(key_tuple);
            std::string label = std::get<3>(key_tuple);

            CPubKey pubkey = key.GetPubKey();
            CHECK_NONFATAL(key.VerifyPubKey(pubkey));
            CKeyID keyid = pubkey.GetID();

            pwallet->WalletLogPrintf("Importing %s...\n", EncodeDestination(PKHash(keyid)));

            if (!pwallet->ImportPrivKeys({{keyid, key}}, time)) {
                pwallet->WalletLogPrintf("Error importing key for %s\n", EncodeDestination(PKHash(keyid)));
                fGood = false;
                continue;
            }

            if (has_label)
                pwallet->SetAddressBook(PKHash(keyid), label, "receive");

            nTimeBegin = std::min(nTimeBegin, time);
            progress++;
        }
        for (const auto& script_pair : scripts) {
            pwallet->chain().showProgress("", std::max(50, std::min(75, (int)((progress / total) * 100) + 50)), false);
            const CScript& script = script_pair.first;
            int64_t time = script_pair.second;

            if (!pwallet->ImportScripts({script}, time)) {
                pwallet->WalletLogPrintf("Error importing script %s\n", HexStr(script));
                fGood = false;
                continue;
            }
            if (time > 0) {
                nTimeBegin = std::min(nTimeBegin, time);
            }

            progress++;
        }
        pwallet->chain().showProgress("", 100, false); // hide progress dialog in GUI
    }
    pwallet->chain().showProgress("", 100, false); // hide progress dialog in GUI
    RescanWallet(*pwallet, reserver, nTimeBegin, false /* update */);
    pwallet->MarkDirty();

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys/scripts to wallet");

    return NullUniValue;