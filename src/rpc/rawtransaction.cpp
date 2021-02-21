// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <index/txindex.h>
#include <key_io.h>
#include <merkleblock.h>
#include <node/blockstorage.h>
#include <node/coin.h>
#include <node/context.h>
#include <node/psbt.h>
#include <node/transaction.h>
#include <policy/packages.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <random.h>
#include <rpc/blockchain.h>
#include <rpc/rawtransaction_util.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/standard.h>
#include <uint256.h>
#include <util/bip32.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <validation.h>
#include <validationinterface.h>

#include <numeric>
#include <stdint.h>

#include <univalue.h>

static void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry, CChainState& active_chainstate)
{
    // Call into TxToUniv() in bitcoin-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in bitcoin-common, so we query them here and push the
    // data into the returned UniValue.
    TxToUniv(tx, uint256(), entry, true, RPCSerializationFlags());

    if (!hashBlock.IsNull()) {
        LOCK(cs_main);

        entry.pushKV("blockhash", hashBlock.GetHex());
        CBlockIndex* pindex = active_chainstate.m_blockman.LookupBlockIndex(hashBlock);
        if (pindex) {
            if (active_chainstate.m_chain.Contains(pindex)) {
                entry.pushKV("confirmations", 1 + active_chainstate.m_chain.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            }
            else
                entry.pushKV("confirmations", 0);
        }
    }
}

static std::vector<RPCArg> CreateTxDoc()
{
    return {
        {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The inputs",
            {
                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                    {
                        {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                        {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        {"sequence", RPCArg::Type::NUM, RPCArg::DefaultHint{"depends on the value of the 'replaceable' and 'locktime' arguments"}, "The sequence number"},
                    },
                },
            },
        },
        {"outputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The outputs (key-value pairs), where none of the keys are duplicated.\n"
                "That is, each address can only appear once and there can only be one 'data' object.\n"
                "For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also\n"
                "                             accepted as second parameter.",
            {
                {"", RPCArg::Type::OBJ_USER_KEYS, RPCArg::Optional::OMITTED, "",
                    {
                        {"address", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "A key-value pair. The key (string) is the bitcoin address, the value (float or string) is the amount in " + CURRENCY_UNIT},
                    },
                },
                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                    {
                        {"data", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "A key-value pair. The key must be \"data\", the value is hex-encoded data"},
                    },
                },
            },
        },
        {"locktime", RPCArg::Type::NUM, RPCArg::Default{0}, "Raw locktime. Non-0 value also locktime-activates inputs"},
        {"replaceable", RPCArg::Type::BOOL, RPCArg::Default{false}, "Marks this transaction as BIP125-replaceable.\n"
                "Allows this transaction to be replaced by a transaction with higher fees. If provided, it is an error if explicit sequence numbers are incompatible."},
    };
}

static RPCHelpMan getrawtransaction()
{
    return RPCHelpMan{
                "getrawtransaction",
                "\nReturn the raw transaction data.\n"

                "\nBy default, this call only returns a transaction if it is in the mempool. If -txindex is enabled\n"
                "and no blockhash argument is passed, it will return the transaction if it is in the mempool or any block.\n"
                "If a blockhash argument is passed, it will return the transaction if\n"
                "the specified block is available and the transaction is in that block.\n"
                "\nHint: Use gettransaction for wallet transactions.\n"

                "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
                "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                    {"verbose", RPCArg::Type::BOOL, RPCArg::Default{false}, "If false, return a string, otherwise return a json object"},
                    {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED_NAMED_ARG, "The block in which to look for the transaction"},
                },
                {
                    RPCResult{"if verbose is not set or set to false",
                         RPCResult::Type::STR, "data", "The serialized, hex-encoded data for 'txid'"
                     },
                     RPCResult{"if verbose is set to true",
                         RPCResult::Type::OBJ, "", "",
                         {
                             {RPCResult::Type::BOOL, "in_active_chain", /*optional=*/true, "Whether specified block is in the active chain or not (only present with explicit \"blockhash\" argument)"},
                             {RPCResult::Type::STR_HEX, "hex", "The serialized, hex-encoded data for 'txid'"},
                             {RPCResult::Type::STR_HEX, "txid", "The transaction id (same as provided)"},
                             {RPCResult::Type::STR_HEX, "hash", "The transaction hash (differs from txid for witness transactions)"},
                             {RPCResult::Type::NUM, "size", "The serialized transaction size"},
                             {RPCResult::Type::NUM, "vsize", "The virtual transaction size (differs from size for witness transactions)"},
                             {RPCResult::Type::NUM, "weight", "The transaction's weight (between vsize*4-3 and vsize*4)"},
                             {RPCResult::Type::NUM, "version", "The version"},
                             {RPCResult::Type::NUM_TIME, "locktime", "The lock time"},
                             {RPCResult::Type::ARR, "vin", "",
                             {
                                 {RPCResult::Type::OBJ, "", "",
                                 {
                                     {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
                                     {RPCResult::Type::NUM, "vout", "The output number"},
                                     {RPCResult::Type::OBJ, "scriptSig", "The script",
                                     {
                                         {RPCResult::Type::STR, "asm", "asm"},
                                         {RPCResult::Type::STR_HEX, "hex", "hex"},
                                     }},
                                     {RPCResult::Type::NUM, "sequence", "The script sequence number"},
                                     {RPCResult::Type::ARR, "txinwitness", /*optional=*/true, "",
                                     {
                                         {RPCResult::Type::STR_HEX, "hex", "hex-encoded witness data (if any)"},
                                     }},
                                 }},
                             }},
                             {RPCResult::Type::ARR, "vout", "",
                             {
                                 {RPCResult::Type::OBJ, "", "",
                                 {
                                     {RPCResult::Type::NUM, "value", "The value in " + CURRENCY_UNIT},
                                     {RPCResult::Type::NUM, "n", "index"},
                                     {RPCResult::Type::OBJ, "scriptPubKey", "",
                                     {
                                         {RPCResult::Type::STR, "asm", "the asm"},
                                         {RPCResult::Type::STR, "hex", "the hex"},
                                         {RPCResult::Type::STR, "type", "The type, eg 'pubkeyhash'"},
                                         {RPCResult::Type::STR, "address", /*optional=*/true, "The Bitcoin address (only if a well-defined address exists)"},
                                     }},
                                 }},
                             }},
                             {RPCResult::Type::STR_HEX, "blockhash", /*optional=*/true, "the block hash"},
                             {RPCResult::Type::NUM, "confirmations", /*optional=*/true, "The confirmations"},
                             {RPCResult::Type::NUM_TIME, "blocktime", /*optional=*/true, "The block time expressed in " + UNIX_EPOCH_TIME},
                             {RPCResult::Type::NUM, "time", /*optional=*/true, "Same as \"blocktime\""},
                        }
                    },
                },
                RPCExamples{
                    HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" false \"myblockhash\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true \"myblockhash\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);

    bool in_active_chain = true;
    uint256 hash = ParseHashV(request.params[0], "parameter 1");
    CBlockIndex* blockindex = nullptr;

    if (hash == Params().GenesisBlock().hashMerkleRoot) {
        // Special exception for the genesis block coinbase transaction
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "The genesis block coinbase is not considered an ordinary transaction and cannot be retrieved");
    }

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        fVerbose = request.params[1].isNum() ? (request.params[1].get_int() != 0) : request.params[1].get_bool();
    }

    if (!request.params[2].isNull()) {
        LOCK(cs_main);

        uint256 blockhash = ParseHashV(request.params[2], "parameter 3");
        blockindex = chainman.m_blockman.LookupBlockIndex(blockhash);
        if (!blockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
        }
        in_active_chain = chainman.ActiveChain().Contains(blockindex);
    }

    bool f_txindex_ready = false;
    if (g_txindex && !blockindex) {
        f_txindex_ready = g_txindex->BlockUntilSyncedToCurrentChain();
    }

    uint256 hash_block;
    const CTransactionRef tx = GetTransaction(blockindex, node.mempool.get(), hash, Params().GetConsensus(), hash_block);
    if (!tx) {
        std::string errmsg;
        if (blockindex) {
            if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
            }
            errmsg = "No such transaction found in the provided block";
        } else if (!g_txindex) {
            errmsg = "No such mempool transaction. Use -txindex or provide a block hash to enable blockchain transaction queries";
        } else if (!f_txindex_ready) {
            errmsg = "No such mempool transaction. Blockchain transactions are still in the process of being indexed";
        } else {
            errmsg = "No such mempool or blockchain transaction";
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
    }

    if (!fVerbose) {
        return EncodeHexTx(*tx, RPCSerializationFlags());
    }

    UniValue result(UniValue::VOBJ);
    if (blockindex) result.pushKV("in_active_chain", in_active_chain);
    TxToJSON(*tx, hash_block, result, chainman.ActiveChainstate());
    return result;
},
    };
}

static RPCHelpMan gettxoutproof()
{
    return RPCHelpMan{"gettxoutproof",
                "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
                "\nNOTE: By default this function only works sometimes. This is when there is an\n"
                "unspent output in the utxo for this transaction. To make it always work,\n"
                "you need to maintain a transaction index, using the -txindex command line option or\n"
                "specify the block in which the transaction is included manually (by blockhash).\n",
                {
                    {"txids", RPCArg::Type::ARR, RPCArg::Optional::NO, "The txids to filter",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "A transaction hash"},
                        },
                        },
                    {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED_NAMED_ARG, "If specified, looks for txid in the block with this hash"},
                },
                RPCResult{
                    RPCResult::Type::STR, "data", "A string that is a serialized, hex-encoded data for the proof."
                },
                RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::set<uint256> setTxids;
    UniValue txids = request.params[0].get_array();
    if (txids.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Parameter 'txids' cannot be empty");
    }
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        auto ret = setTxids.insert(ParseHashV(txids[idx], "txid"));
        if (!ret.second) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated txid: ") + txids[idx].get_str());
        }
    }

    CBlockIndex* pblockindex = nullptr;
    uint256 hashBlock;
    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    if (!request.params[1].isNull()) {
        LOCK(cs_main);
        hashBlock = ParseHashV(request.params[1], "blockhash");
        pblockindex = chainman.m_blockman.LookupBlockIndex(hashBlock);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    } else {
        LOCK(cs_main);
        CChainState& active_chainstate = chainman.ActiveChainstate();

        // Loop through txids and try to find which block they're in. Exit loop once a block is found.
        for (const auto& tx : setTxids) {
            const Coin& coin = AccessByTxid(active_chainstate.CoinsTip(), tx);
            if (!coin.IsSpent()) {
                pblockindex = active_chainstate.m_chain[coin.nHeight];
                break;
            }
        }
    }


    // Allow txindex to catch up if we need to query it and before we acquire cs_main.
    if (g_txindex && !pblockindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    LOCK(cs_main);

    if (pblockindex == nullptr) {
        const CTransactionRef tx = GetTransaction(/* block_index */ nullptr, /* mempool */ nullptr, *setTxids.begin(), Params().GetConsensus(), hashBlock);
        if (!tx || hashBlock.IsNull()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        }
        pblockindex = chainman.m_blockman.LookupBlockIndex(hashBlock);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        }
    }

    CBlock block;
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
    }

    unsigned int ntxFound = 0;
    for (const auto& tx : block.vtx) {
        if (setTxids.count(tx->GetHash())) {
            ntxFound++;
        }
    }
    if (ntxFound != setTxids.size()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not all transactions found in specified or retrieved block");
    }

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB);
    return strHex;
},
    };
}

static RPCHelpMan verifytxoutproof()
{
    return RPCHelpMan{"verifytxoutproof",
                "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
                "and throwing an RPC error if the block is not in our best chain\n",
                {
                    {"proof", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hex-encoded proof generated by gettxoutproof"},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "txid", "The txid(s) which the proof commits to, or empty array if the proof can not be validated."},
                    }
                },
                RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot)
        return res;

    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    LOCK(cs_main);

    const CBlockIndex* pindex = chainman.m_blockman.LookupBlockIndex(merkleBlock.header.GetHash());
    if (!pindex || !chainman.ActiveChain().Contains(pindex) || pindex->nTx == 0) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");
    }

    // Check if proof is valid, only add results if so
    if (pindex->nTx == merkleBlock.txn.GetNumTransactions()) {
        for (const uint256& hash : vMatch) {
            res.push_back(hash.GetHex());
        }
    }

    return res;
},
    };
}

static RPCHelpMan createrawtransaction()
{
    return RPCHelpMan{"createrawtransaction",
                "\nCreate a transaction spending the given inputs and creating new outputs.\n"
                "Outputs can be addresses or data.\n"
                "Returns hex-encoded raw transaction.\n"
                "Note that the transaction's inputs are not signed, and\n"
                "it is not stored in the wallet or transmitted to the network.\n",
                CreateTxDoc(),
                RPCResult{
                    RPCResult::Type::STR_HEX, "transaction", "hex string of the transaction"
                },
                RPCExamples{
                    HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"address\\\":0.01}]\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"[{\\\"address\\\":0.01}]\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"[{\\\"data\\\":\\\"00010203\\\"}]\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    RPCTypeCheck(request.params, {
        UniValue::VARR,
        UniValueType(), // ARR or OBJ, checked later
        UniValue::VNUM,
        UniValue::VBOOL
        }, true
    );

    bool rbf = false;
    if (!request.params[3].isNull()) {
        rbf = request.params[3].isTrue();
    }
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], request.params[1], request.params[2], rbf);

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

static RPCHelpMan decoderawtransaction()
{
    return RPCHelpMan{"decoderawtransaction",
                "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n",
                {
                    {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction hex string"},
                    {"iswitness", RPCArg::Type::BOOL, RPCArg::DefaultHint{"depends on heuristic tests"}, "Whether the transaction hex is a serialized witness transaction.\n"
                        "If iswitness is not present, heuristic tests will be used in decoding.\n"
                        "If true, only witness deserialization will be tried.\n"
                        "If false, only non-witness deserialization will be tried.\n"
                        "This boolean should reflect whether the transaction has inputs\n"
                        "(e.g. fully valid, or on-chain transactions), if known by the caller."
                    },
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
                        {RPCResult::Type::STR_HEX, "hash", "The transaction hash (differs from txid for witness transactions)"},
                        {RPCResult::Type::NUM, "size", "The transaction size"},
                        {RPCResult::Type::NUM, "vsize", "The virtual transaction size (differs from size for witness transactions)"},
                        {RPCResult::Type::NUM, "weight", "The transaction's weight (between vsize*4 - 3 and vsize*4)"},
                        {RPCResult::Type::NUM, "version", "The version"},
                        {RPCResult::Type::NUM_TIME, "locktime", "The lock time"},
                        {RPCResult::Type::ARR, "vin", "",
                        {
                            {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR_HEX, "coinbase", /*optional=*/true, ""},
                                {RPCResult::Type::STR_HEX, "txid", /*optional=*/true, "The transaction id"},
                                {RPCResult::Type::NUM, "vout", /*optional=*/true, "The output number"},
                                {RPCResult::Type::OBJ, "scriptSig", /*optional=*/true, "The script",
                                {
                                    {RPCResult::Type::STR, "asm", "asm"},
                                    {RPCResult::Type::STR_HEX, "hex", "hex"},
                                }},
                                {RPCResult::Type::ARR, "txinwitness", /*optional=*/true, "",
                                {
                                    {RPCResult::Type::STR_HEX, "hex", "hex-encoded witness data (if any)"},
                                }},
                                {RPCResult::Type::NUM, "sequence", "The script sequence number"},
                            }},
                        }},
                        {RPCResult::Type::ARR, "vout", "",
                        {
                            {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::NUM, "value", "The value in " + CURRENCY_UNIT},
                                {RPCResult::Type::NUM, "n", "index"},
                                {RPCResult::Type::OBJ, "scriptPubKey", "",
                                {
                                    {RPCResult::Type::STR, "asm", "the asm"},
                                    {RPCResult::Type::STR_HEX, "hex", "the hex"},
                                    {RPCResult::Type::STR, "type", "The type, eg 'pubkeyhash'"},
                                    {RPCResult::Type::STR, "address", /*optional=*/true, "The Bitcoin address (only if a well-defined address exists)"},
                                }},
                            }},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    CMutableTransaction mtx;

    bool try_witness = request.params[1].isNull() ? true : request.params[1].get_bool();
    bool try_no_witness = request.params[1].isNull() ? true : !request.params[1].get_bool();

    if (!DecodeHexTx(mtx, request.params[0].get_str(), try_no_witness, try_witness)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    UniValue result(UniValue::VOBJ);
    TxToUniv(CTransaction(std::move(mtx)), uint256(), result, false);

    return result;
},
    };
}

static std::string GetAllOutputTypes()
{
    std::vector<std::string> ret;
    using U = std::underlying_type<TxoutType>::type;
    for (U i = (U)TxoutType::NONSTANDARD; i <= (U)TxoutType::WITNESS_UNKNOWN; ++i) {
        ret.emplace_back(GetTxnOutputType(static_cast<TxoutType>(i)));
    }
    return Join(ret, ", ");
}

static RPCHelpMan decodescript()
{
    return RPCHelpMan{
        "decodescript",
        "\nDecode a hex-encoded script.\n",
        {
            {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded script"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "asm", "Script public key"},
                {RPCResult::Type::STR, "type", "The output type (e.g. " + GetAllOutputTypes() + ")"},
                {RPCResult::Type::STR, "address", /*optional=*/true, "The Bitcoin address (only if a well-defined address exists)"},
                {RPCResult::Type::STR, "p2sh", /*optional=*/true,
                 "address of P2SH script wrapping this redeem script (not returned for types that should not be wrapped)"},
                {RPCResult::Type::OBJ, "segwit", /*optional=*/true,
                 "Result of a witness script public key wrapping this redeem script (not returned for types that should not be wrapped)",
                 {
                     {RPCResult::Type::STR, "asm", "String representation of the script public key"},
                     {RPCResult::Type::STR_HEX, "hex", "Hex string of the script public key"},
                     {RPCResult::Type::STR, "type", "The type of the script public key (e.g. witness_v0_keyhash or witness_v0_scripthash)"},
                     {RPCResult::Type::STR, "address", /*optional=*/true, "The Bitcoin address (only if a well-defined address exists)"},
                     {RPCResult::Type::STR, "p2sh-segwit", "address of the P2SH script wrapping this witness redeem script"},
                 }},
            },
        },
        RPCExamples{
            HelpExampleCli("decodescript", "\"hexstring\"")
          + HelpExampleRpc("decodescript", "\"hexstring\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    RPCTypeCheck(request.params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0){
        std::vector<unsigned char> scriptData(ParseHexV(request.params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToUniv(script, r, /* include_hex */ false);

    std::vector<std::vector<unsigned char>> solutions_data;
    const TxoutType which_type{Solver(script, solutions_data)};

    const bool can_wrap{[&] {
        switch (which_type) {
        case TxoutType::MULTISIG:
        case TxoutType::NONSTANDARD:
        case TxoutType::PUBKEY:
        case TxoutType::PUBKEYHASH:
        case TxoutType::WITNESS_V0_KEYHASH:
        case TxoutType::WITNESS_V0_SCRIPTHASH:
            // Can be wrapped if the checks below pass
            break;
        case TxoutType::NULL_DATA:
        case TxoutType::SCRIPTHASH:
        case TxoutType::WITNESS_UNKNOWN:
        case TxoutType::WITNESS_V1_TAPROOT:
            // Should not be wrapped
            return false;
        } // no default case, so the compiler can warn about missing cases
        if (!script.HasValidOps() || script.IsUnspendable()) {
            return false;
        }
        for (CScript::const_iterator it{script.begin()}; it != script.end();) {
            opcodetype op;
            CHECK_NONFATAL(script.GetOp(it, op));
            if (op == OP_CHECKSIGADD || IsOpSuccess(op)) {
                return false;
            }
        }
        return true;
    }()};

    if (can_wrap) {
        r.pushKV("p2sh", EncodeDestination(ScriptHash(script)));
        // P2SH and witness programs cannot be wrapped in P2WSH, if this script
        // is a witness program, don't return addresses for a segwit programs.
        const bool can_wrap_P2WSH{[&] {
            switch (which_type) {
            case TxoutType::MULTISIG:
            case TxoutType::PUBKEY:
            // Uncompressed pubkeys cannot be used with segwit checksigs.
            // If the script contains an uncompressed pubkey, skip encoding of a segwit program.
                for (const auto& solution : solutions_data) {
                    if