// Copyright (c) 2018-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparamsbase.h>
#include <external_signer.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>
#include <rpc/protocol.h>

#include <string>
#include <vector>

#ifdef ENABLE_EXTERNAL_SIGNER

static RPCHelpMan enumeratesigners()
{
    return RPCHelpMan{"enumeratesigners",
        "Returns a list of external signers from -signer.",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::ARR, "signers", /* optional */ false, "",
                {
                    {RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "fingerprint", "Master key fingerprint"},
                        {RPCResult::Type::STR, "name", "Device name"},
                    }},
                },
                }
            }
        },
        RPCExamples{
            HelpExampleCli("enumeratesigners", "")
            + HelpExampleRpc("enumeratesigners", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const std::string command = gArgs.GetArg("-signer", "");
            if (command == "") throw JSONRPCError(RPC_MISC_ERROR, "Error: restart bitcoind with -signer=<cmd>");
            const st