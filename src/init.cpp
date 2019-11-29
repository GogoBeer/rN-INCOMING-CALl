// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <init.h>

#include <addrman.h>
#include <banman.h>
#include <blockfilter.h>
#include <chain.h>
#include <chainparams.h>
#include <compat/sanity.h>
#include <consensus/amount.h>
#include <deploymentstatus.h>
#include <fs.h>
#include <hash.h>
#include <httprpc.h>
#include <httpserver.h>
#include <index/blockfilterindex.h>
#include <index/coinstatsindex.h>
#include <index/txindex.h>
#include <init/common.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <mapport.h>
#include <net.h>
#include <net_permissions.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/blockstorage.h>
#include <node/caches.h>
#include <node/chainstate.h>
#include <node/context.h>
#include <node/miner.h>
#include <node/ui_interface.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <protocol.h>
#include <rpc/blockchain.h>
#include <rpc/register.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <scheduler.h>
#include <script/sigcache.h>
#include <script/standard.h>
#include <shutdown.h>
#include <sync.h>
#include <timedata.h>
#include <torcontrol.h>
#include <txdb.h>
#include <txmempool.h>
#include <txorphanage.h>
#include <util/asmap.h>
#include <util/check.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/syscall_sandbox.h>
#include <util/system.h>
#include <util/thread.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>
#include <walletinitinterface.h>

#include <functional>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <thread>
#include <vector>

#ifndef WIN32
#include <attributes.h>
#include <cerrno>
#include <signal.h>
#include <sys/stat.h>
#endif

#include <boost/algorithm/string/replace.hpp>
#include <boost/signals2/signal.hpp>

#if ENABLE_ZMQ
#include <zmq/zmqabstractnotifier.h>
#include <zmq/zmqnotificationinterface.h>
#include <zmq/zmqrpc.h>
#endif

static const bool DEFAULT_PROXYRANDOMIZE = true;
static const bool DEFAULT_REST_ENABLE = false;

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

static const char* DEFAULT_ASMAP_FILENAME="ip_asn.map";

/**
 * The PID file facilities.
 */
static const char* BITCOIN_PID_FILENAME = "bitcoind.pid";

static fs::path GetPidFile(const ArgsManager& args)
{
    return AbsPathForConfigVal(fs::PathFromString(args.GetArg("-pid", BITCOIN_PID_FILENAME)));
}

[[nodiscard]] static bool CreatePidFile(const ArgsManager& args)
{
    fsbridge::ofstream file{GetPidFile(args)};
    if (file) {
#ifdef WIN32
        tfm::format(file, "%d\n", GetCurrentProcessId());
#else
        tfm::format(file, "%d\n", getpid());
#endif
        return true;
    } else {
        return InitError(strprintf(_("Unable to create the PID file '%s': %s"), fs::PathToString(GetPidFile(args)), std::strerror(errno)));
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets ShutdownRequested(), which makes main thread's
// WaitForShutdown() interrupts the thread group.
// And then, WaitForShutdown() makes all other on-going threads
// in the thread group join the main thread.
// Shutdown() is then called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// ShutdownRequested() getting set, and then does the normal Qt
// shutdown thing.
//

void Interrupt(NodeContext& node)
{
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    InterruptTorControl();
    InterruptMapPort();
    if (node.connman)
        node.connman->Interrupt();
    if (g_txindex) {
        g_txindex->Interrupt();
    }
    ForEachBlockFilterIndex([](BlockFilterIndex& index) { index.Interrupt(); });
    if (g_coin_stats_index) {
        g_coin_stats_index->Interrupt();
    }
}

void Shutdown(NodeContext& node)
{
    static Mutex g_shutdown_mutex;
    TRY_LOCK(g_shutdown_mutex, lock_shutdown);
    if (!lock_shutdown) return;
    LogPrintf("%s: In progress...\n", __func__);
    Assert(node.args);

    /// Note: Shutdown() must be able to handle cases in which initialization failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    util::ThreadRename("shutoff");
    if (node.mempool) node.mempool->AddTransactionsUpdated(1);

    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
    for (const auto& client : node.chain_clients) {
        client->flush();
    }
    StopMapPort();

    // Because these depend on each-other, we make sure that neither can be
    // using the other before destroying them.
    if (node.peerman) UnregisterValidationInterface(node.peerman.get());
    if (node.connman) node.connman->Stop();

    StopTorControl();

    // After everything has been shut down, but before things get flushed, stop the
    // CScheduler/checkqueue, scheduler and load block thread.
    if (node.scheduler) node.scheduler->stop();
    if (node.chainman && node.chainman->m_load_block.joinable()) node.chainman->m_load_block.join();
    StopScriptCheckWorkerThreads();

    // After the threads that potentially access these pointers have been stopped,
    // destruct and reset all to nullptr.
    node.peerman.reset();
    node.connman.reset();
    node.banman.reset();
    node.addrman.reset();

    if (node.mempool && node.mempool->IsLoaded() && node.args->GetBoolArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        DumpMempool(*node.mempool);
    }

    // Drop transactions we were still watching, and record fee estimations.
    if (node.fee_estimator) node.fee_estimator->Flush();

    // FlushStateToDisk generates a ChainStateFlushed callback, which we should avoid missing
    if (node.chainman) {
        LOCK(cs_main);
        for (CChainState* chainstate : node.chainman->GetAll()) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
            }
        }
    }

    // After there are no more peers/RPC left to give us new data which may generate
    // CValidationInterface callbacks, flush them...
    GetMainSignals().FlushBackgroundCallbacks();

    // Stop and delete all indexes only after flushing background callbacks.
    if (g_txindex) {
        g_txindex->Stop();
        g_txindex.reset();
    }
    if (g_coin_stats_index) {
        g_coin_stats_index->Stop();
        g_coin_stats_index.reset();
    }
    ForEachBlockFilterIndex([](BlockFilterIndex& index) { index.Stop(); });
    DestroyAllBlockFilterIndexes();

    // Any future callbacks will be dropped. This should absolutely be safe - if
    // missing a callback results in an unrecoverable situation, unclean shutdown
    // would too. The only reason to do the above flushes is to let the wallet catch
    // up with our current chain to avoid any strange pruning edge cases and make
    // next startup faster by avoiding rescan.

    if (node.chainman) {
        LOCK(cs_main);
        for (CChainState* chainstate : node.chainman->GetAll()) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
                chainstate->ResetCoinsViews();
            }
        }
    }
    for (const auto& client : node.chain_clients) {
        client->stop();
    }

#if ENABLE_ZMQ
    if (g_zmq_notification_interface) {
        UnregisterValidationInterface(g_zmq_notification_interface);
        delete g_zmq_notification_interface;
        g_zmq_notification_interface = nullptr;
    }
#endif

    node.chain_clients.clear();
    UnregisterAllValidationInterfaces();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
    init::UnsetGlobals();
    node.mempool.reset();
    node.fee_estimator.reset();
    node.chainman.reset();
    node.scheduler.reset();

    try {
        if (!fs::remove(GetPidFile(*node.args))) {
            LogPrintf("%s: Unable to remove PID file: File does not exist\n", __func__);
        }
    } catch (const fs::filesystem_error& e) {
        LogPrintf("%s: Unable to remove PID file: %s\n", __func__, fsbridge::get_filesystem_error_message(e));
    }

    node.args = nullptr;
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do.
 * The execution context the handler is invoked in is not guaranteed,
 * so we restrict handler operations to just touching variables:
 */
#ifndef WIN32
static void HandleSIGTERM(int)
{
    StartShutdown();
}

static void HandleSIGHUP(int)
{
    LogInstance().m_reopen_file = true;
}
#else
static BOOL WINAPI consoleCtrlHandler(DWORD dwCtrlType)
{
    StartShutdown();
    Sleep(INFINITE);
    return true;
}
#endif

#ifndef WIN32
static void registerSignalHandler(int signal, void(*handler)(int))
{
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signal, &sa, nullptr);
}
#endif

static boost::signals2::connection rpc_notify_block_change_connection;
static void OnRPCStarted()
{
    rpc_notify_block_change_connection = uiInterface.NotifyBlockTip_connect(std::bind(RPCNotifyBlockChange, std::placeholders::_2));
}

static void OnRPCStopped()
{
    rpc_notify_block_change_connection.disconnect();
    RPCNotifyBlockChange(nullptr);
    g_best_block_cv.notify_all();
    LogPrint(BCLog::RPC, "RPC stopped.\n");
}

void SetupServerArgs(ArgsManager& argsman)
{
    SetupHelpOptions(argsman);
    argsman.AddArg("-help-debug", "Print help message with debugging options and exit", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST); // server-only for now

    init::AddLoggingArgs(argsman);

    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto signetBaseParams = CreateBaseChainParams(CBaseChainParams::SIGNET);
    const auto regtestBaseParams = CreateBaseChainParams(CBaseChainParams::REGTEST);
    const auto defaultChainParams = CreateChainParams(argsman, CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(argsman, CBaseChainParams::TESTNET);
    const auto signetChainParams = CreateChainParams(argsman, CBaseChainParams::SIGNET);
    const auto regtestChainParams = CreateChainParams(argsman, CBaseChainParams::REGTEST);

    // Hidden Options
    std::vector<std::string> hidden_args = {
        "-dbcrashratio", "-forcecompactdb",
        // GUI args. These will be overwritten by SetupUIArgs for the GUI
        "-choosedatadir", "-lang=<lang>", "-min", "-resetguisettings", "-splash", "-uiplatform"};

    argsman.AddArg("-version", "Print version and exit", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#if HAVE_SYSTEM
    argsman.AddArg("-alertnotify=<cmd>", "Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
    argsman.AddArg("-assumevalid=<hex>", strprintf("If this block is in the chain assume that it and its ancestors are valid and potentially skip their script verification (0 to verify all, default: %s, testnet: %s, signet: %s)", defaultChainParams->GetConsensus().defaultAssumeValid.GetHex(), testnetChainParams->GetConsensus().defaultAssumeValid.GetHex(), signetChainParams->GetConsensus().defaultAssumeValid.GetHex()), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-blocksdir=<dir>", "Specify directory to hold blocks subdirectory for *.dat files (default: <datadir>)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-fastprune", "Use smaller block files and lower minimum prune height for testing purposes", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
#if HAVE_SYSTEM
    argsman.AddArg("-blocknotify=<cmd>", "Execute command when the best block changes (%s in cmd is replaced by block hash)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
    argsman.AddArg("-blockreconstructionextratxn=<n>", strprintf("Extra transactions to keep in memory for compact block reconstructions (default: %u)", DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-blocksonly", strprintf("Whether to reject transactions from network peers. Automatic broadcast and rebroadcast of any transactions from inbound peers is disabled, unless the peer has the 'forcerelay' permission. RPC transactions are not affected. (default: %u)", DEFAULT_BLOCKSONLY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-coinstatsindex", strprintf("Maintain coinstats index used by the gettxoutsetinfo RPC (default: %u)", DEFAULT_COINSTATSINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-conf=<file>", strprintf("Specify path to read-only configuration file. Relative paths will be prefixed by datadir location. (default: %s)", BITCOIN_CONF_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-datadir=<dir>", "Specify data directory", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-dbbatchsize", strprintf("Maximum database write batch size in bytes (default: %u)", nDefaultDbBatchSize), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    argsman.AddArg("-dbcache=<n>", strprintf("Maximum database cache size <n> MiB (%d to %d, default: %d). In addition, unused mempool memory is shared for this cache (see -maxmempool).", nMinDbCache, nMaxDbCache, nDefaultDbCache), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-includeconf=<file>", "Specify additional configuration file, relative to the -datadir path (only useable from configuration file, not command line)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-loadblock=<file>", "Imports blocks from external file on startup", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-maxmempool=<n>", strprintf("Keep the transaction memory pool below <n> megabytes (default: %u)", DEFAULT_MAX_MEMPOOL_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-maxorphantx=<n>", strprintf("Keep at most <n> unconnectable transactions in memory (default: %u)", DEFAULT_MAX_ORPHAN_TRANSACTIONS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-mempoolexpiry=<n>", strprintf("Do not keep transactions in the mempool longer than <n> hours (default: %u)", DEFAULT_MEMPOOL_EXPIRY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-minimumchainwork=<hex>", strprintf("Minimum work assumed to exist on a valid chain in hex (default: %s, testnet: %s, signet: %s)", defaultChainParams->GetConsensus().nMinimumChainWork.GetHex(), testnetChainParams->GetConsensus().nMinimumChainWork.GetHex(), signetChainParams->GetConsensus().nMinimumChainWork.GetHex()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    argsman.AddArg("-par=<n>", strprintf("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)",
        -GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-persistmempool", strprintf("Whether to save the mempool on shutdown and load on restart (default: %u)", DEFAULT_PERSIST_MEMPOOL), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-pid=<file>", strprintf("Specify pid file. Relative paths will be prefixed by a net-specific datadir location. (default: %s)", BITCOIN_PID_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-prune=<n>", strprintf("Reduce storage requirements by enabling pruning (deleting) of old blocks. This allows the pruneblockchain RPC to be called to delete specific blocks, and enables automatic pruning of old blocks if a target size in MiB is provided. This mode is incompatible with -txindex and -coinstatsindex. "
            "Warning: Reverting this setting requires re-downloading the entire blockchain. "
            "(default: 0 = disable pruning blocks, 1 = allow manual pruning via RPC, >=%u = automatically prune block files to stay under the specified target size in MiB)", MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-reindex", "Rebuild chain state and block index from the blk*.dat files on disk", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-reindex-chainstate", "Rebuild chain state from the currently indexed blocks. When in pruning mode or if blocks on disk might be corrupted, use full -reindex instead.", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-settings=<file>", strprintf("Specify path to dynamic settings data file. Can be disabled with -nosettings. File is written at runtime and not meant to be edited by users (use %s instead for custom settings). Relative paths will be prefixed by datadir location. (default: %s)", BITCOIN_CONF_FILENAME, BITCOIN_SETTINGS_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#if HAVE_SYSTEM
    argsman.AddArg("-startupnotify=<cmd>", "Execute command on startup.", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
#ifndef WIN32
    argsman.AddArg("-sysperms", "Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-sysperms");
#endif
    argsman.AddArg("-txindex", strprintf("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)", DEFAULT_TXINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-blockfilterindex=<type>",
                 strprintf("Maintain an index of compact filters by block (default: %s, values: %s).", DEFAULT_BLOCKFILTERINDEX, ListBlockFilterTypes()) +
                 " If <type> is not supplied or if <type> = 1, indexes for all known types are enabled.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);

    argsman.AddArg("-addnode=<ip>", strprintf("Add a node to connect to and attempt to keep the connection open (see the addnode RPC help for more info). This option can be specified multiple times to add multiple nodes; connections are limited to %u at a time and are counted separately from the -maxconnections limit.", MAX_ADDNODE_CONNECTIONS), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-asmap=<file>", strprintf("Specify asn mapping used for bucketing of the peers (default: %s). Relative paths will be prefixed by the net-specific datadir location.", DEFAULT_ASMAP_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-bantime=<n>", strprintf("Default duration (in seconds) of manually configured bans (default: %u)", DEFAULT_MISBEHAVING_BANTIME), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-bind=<addr>[:<port>][=onion]", strprintf("Bind to given address and always listen on it (default: 0.0.0.0). Use [host]:port notation for IPv6. Append =onion to tag any incoming connections to that address and port as incoming Tor connections (default: 127.0.0.1:%u=onion, testnet: 127.0.0.1:%u=onion, signet: 127.0.0.1:%u=onion, regtest: 127.0.0.1:%u=onion)", defaultBaseParams->OnionServiceTargetPort(), testnetBaseParams->OnionServiceTargetPort(), signetBaseParams->OnionServiceTargetPort(), regtestBaseParams->OnionServiceTargetPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-cjdnsreachable", "If set then this host is configured for CJDNS (connecting to fc00::/8 addresses would lead us to the CJDNS network) (default: 0)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-connect=<ip>", "Connect only to the specified node; -noconnect disables automatic connections (the rules for this peer are the same as for -addnode). This option can be specified multiple times to connect to multiple nodes.", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-discover", "Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-dns", strprintf("Allow DNS lookups for -addnode, -seednode and -connect (default: %u)", DEFAULT_NAME_LOOKUP), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-dnsseed", strprintf("Query for peer addresses via DNS lookup, if low on addresses (default: %u unless -connect used)", DEFAULT_DNSSEED), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-externalip=<ip>", "Specify your own public address", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-fixedseeds", strprintf("Allow fixed seeds if DNS seeds don't provide peers (default: %u)", DEFAULT_FIXEDSEEDS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-forcednsseed", strprintf("Always query for peer addresses via DNS lookup (default: %u)", DEFAULT_FORCEDNSSEED), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-listen", "Accept connections from outside (default: 1 if no -proxy or -connect)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-listenonion", strprintf("Automatically create Tor onion service (default: %d)", DEFAULT_LISTEN_ONION), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxconnections=<n>", strprintf("Maintain at most <n> connections to peers (default: %u). This limit does not apply to connections manually added via -addnode or the addnode RPC, which have a separate limit of %u.", DEFAULT_MAX_PEER_CONNECTIONS, MAX_ADDNODE_CONNECTIONS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxreceivebuffer=<n>", strprintf("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXRECEIVEBUFFER), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxsendbuffer=<n>", strprintf("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXSENDBUFFER), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxtimeadjustment", strprintf("Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced by peers forward or backward by this amount. (default: %u seconds)", DEFAULT_MAX_TIME_ADJUSTMENT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxuploadtarget=<n>", strprintf("Tries to keep outbound traffic under the given target per 24h. Limit does not apply to peers with 'download' permission or blocks created within past week. 0 = no limit (default: %s). Optional suffix units [k|K|m|M|g|G|t|T] (default: M). Lowercase is 1000 base while uppercase is 1024 base", DEFAULT_MAX_UPLOAD_TARGET), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-onion=<ip:port>", "Use separate SOCKS5 proxy to reach peers via Tor onion services, set -noonion to disable (default: -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-i2psam=<ip:port>", "I2P SAM proxy to reach I2P peers and accept I2P connections (default: none)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-i2pacceptincoming", "If set and -i2psam is also set then incoming I2P connections are accepted via the SAM proxy. If this is not set but -i2psam is set then only outgoing connections will be made to the I2P network. Ignored if -i2psam is not set. Listening for incoming I2P connections is done through the SAM proxy, not by binding to a local address and port (default: 1)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-onlynet=<net>", "Make outgoing connections only through network <net> (" + Join(GetNetworkNames(), ", ") + "). Incoming connections are not affected by this option. This option can be specified multiple times to allow multiple networks. Warning: if it is used with non-onion networks and the -onion or -proxy option is set, then outbound onion connections will still be made; use -noonion or -onion=0 to disable outbound onion connections in this case.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-peerbloomfilters", strprintf("Support filtering of blocks and transaction with bloom filters (default: %u)", DEFAULT_PEERBLOOMFILTERS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-peerblockfilters", strprintf("Serve compact block filters to peers per BIP 157 (default: %u)", DEFAULT_PEERBLOCKFILTERS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-permitbaremultisig", strprintf("Relay non-P2SH multisig (default: %u)", DEFAULT_PERMIT_BAREMULTISIG), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-port=<port>", strprintf("Listen for connections on <port>. Nodes not using the default ports (default: %u, testnet: %u, signet: %u, regtest: %u) are unlikely to get incoming connections. Not relevant for I2P (see doc/i2p.md).", defaultChainParams->GetDefaultPort(), testnetChainParams->GetDefaultPort(), signetChainParams->GetDefaultPort(), regtestChainParams->GetDefaultPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-proxy=<ip:port>", "Connect through SOCKS5 proxy, set -noproxy to disable (default: disabled)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-proxyrandomize", strprintf("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)", DEFAULT_PROXYRANDOMIZE), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-seednode=<ip>", "Connect to a node to retrieve peer addresses, and disconnect. This option can be specified multiple times to connect to multiple nodes.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-networkactive", "Enable all P2P network activity (default: 1). Can be changed by the setnetworkactive RPC command", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-timeout=<n>", strprintf("Specify socket connection timeout in milliseconds. If an initial attempt to connect is unsuccessful after this amount of time, drop it (minimum: 1, default: %d)", DEFAULT_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-peertimeout=<n>", strprintf("Specify a p2p connection timeout delay in seconds. After connecting to a peer, wait this amount of time before considering disconnection based on inactivity (minimum: 1, default: %d)", DEFAULT_PEER_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-torcontrol=<ip>:<port>", strprintf("Tor control port to use if onion listening enabled (default: %s)", DEFAULT_TOR_CONTROL), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-torpassword=<pass>", "Tor control port password (default: empty)", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::CONNECTION);
#ifdef USE_UPNP
#if USE_UPNP
    argsman.AddArg("-upnp", "Use UPnP to map the listening port (default: 1 when listening and no -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#else
    argsman.AddArg("-upnp", strprintf("Use UPnP to map the listening port (default: %u)", 0), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#endif
#else
    hidden_args.emplace_back("-upnp");
#endif
#ifdef USE_NATPMP
    argsman.AddArg("-natpmp", strprintf("Use NAT-PMP to map the listening port (default: %s)", DEFAULT_NATPMP ? "1 when listening and no -proxy" : "0"), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#else
    hidden_args.emplace_back("-natpmp");
#endif // USE_NATPMP
    argsman.AddArg("-whitebind=<[permissions@]addr>", "Bind to the given address and add permission flags to the peers connecting to it. "
        "Use [host]:port notation for IPv6. Allowed permissions: " + Join(NET_PERMISSIONS_DOC, ", ") + ". "
        "Specify multiple permissions separated by commas (default: download,noban,mempool,relay). Can be specified multiple times.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    argsman.AddArg("-whitelist=<[permissions@]IP address or network>", "Add permission flags to the peers connecting from the given IP address (e.g. 1.2.3.4) or "
        "CIDR-notated network (e.g. 1.2.3.0/24). Uses the same permissions as "
        "-whitebind. Can be specified multiple times." , ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    g_wallet_init_interface.AddWalletOptions(argsman);

#if ENABLE_ZMQ
    argsman.AddArg("-zmqpubhashblock=<address>", "Enable publish hash block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashtx=<address>", "Enable publish hash transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawblock=<address>", "Enable publish raw block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtx=<address>", "Enable publish raw transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubsequence=<address>", "Enable publish hash block and tx sequence in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashblockhwm=<n>", strprintf("Set publish hash block outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashtxhwm=<n>", strprintf("Set publish hash transaction outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawblockhwm=<n>", strprintf("Set publish raw block outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtxhwm=<n>", strprintf("Set publish raw transaction outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubsequencehwm=<n>", strprintf("Set publish hash sequence message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
#else
    hidden_args.emplace_back("-zmqpubhashblock=<address>");
    hidden_args.emplace_back("-zmqpubhashtx=<address>");
    hidden_args.emplace_back("-zmqpubrawblock=<address>");
    hidden_args.emplace_back("-zmqpubrawtx=<address>");
    hidden_args.emplace_back("-zmqpubsequence=<n>");
    hidden_args.emplace_back("-zmqpubhashblockhwm=<n>");
    hidden_args.emplace_back("-zmqpubhashtxhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawblockhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawtxhwm=<n>");
    hidden_args.emplace_back("-zmqpubsequencehwm=<n>");
#endif

    argsman.AddArg("-checkblocks=<n>", strprintf("How many blocks to check at startup (default: %u, 0 = all)", DEFAULT_CHECKBLOCKS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checklevel=<n>", strprintf("How thorough the block verification of -checkblocks is: %s (0-4, default: %u)", Join(CHECKLEVEL_DOC, ", "), DEFAULT_CHECKLEVEL), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkblockindex", strprintf("Do a consistency check for the block tree, chainstate, and other validation data structures occasionally. (default: %u, regtest: %u)", defaultChainParams->DefaultConsistencyChecks(), regtestChainParams->DefaultConsistencyChecks()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkaddrman=<n>", strprintf("Run addrman consistency checks every <n> operations. Use 0 to disable. (default: %u)", DEFAULT_ADDRMAN_CONSISTENCY_CHECKS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkmempool=<n>", strprintf("Run mempool consistency checks every <n> transactions. Use 0 to disable. (default: %u, regtest: %u)", defaultChainParams->DefaultConsistencyChecks(), regtestChainParams->DefaultConsistencyChecks()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkpoints", strprintf("Enable rejection of any forks from the known historical chain until block %s (default: %u)", defaultChainParams->Checkpoints().GetHeight(), DEFAULT_CHECKPOINTS_ENABLED), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-deprecatedrpc=<method>", "Allows deprecated RPC method(s) to be used", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-stopafterblockimport", strprintf("Stop running after importing blocks from disk (default: %u)", DEFAULT_STOPAFTERBLOCKIMPORT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-stopatheight", strprintf("Stop running after reaching the given height in the main chain (default: %u)", DEFAULT_STOPATHEIGHT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitancestorcount=<n>", strprintf("Do not accept transactions if number of in-mempool ancestors is <n> or more (default: %u)", DEFAULT_ANCESTOR_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitancestorsize=<n>", strprintf("Do not accept transactions whose size with all in-mempool ancestors exceeds <n> kilobytes (default: %u)", DEFAULT_ANCESTOR_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitdescendantcount=<n>", strprintf("Do not accept transactions if any ancestor would have <n> or more in-mempool descendants (default: %u)", DEFAULT_DESCENDANT_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitdescendantsize=<n>", strprintf("Do not accept transactions if any ancestor would have more than <n> kilobytes of in-mempool descendants (default: %u).", DEFAULT_DESCENDANT_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-addrmantest", "Allows to test address relay on localhost", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-capturemessages", "Capture all P2P messages to disk", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-mocktime=<n>", "Replace actual time with " + UNIX_EPOCH_TIME + " (default: 0)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-maxsigcachesize=<n>", strprintf("Limit sum of signature cache and script execution cache sizes to <n> MiB (default: %u)", DEFAULT_MAX_SIG_CACHE_SIZE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-maxtipage=<n>", strprintf("Maximum tip age in seconds to consider node in initial block download (default: %u)", DEFAULT_MAX_TIP_AGE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-printpriority", strprintf("Log transaction fee rate in " + CURRENCY_UNIT + "/kvB when mining blocks (default: %u)", DEFAULT_PRINTPRIORITY), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-uacomment=<cmt>", "Append comment to the user agent string", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);

    SetupChainParamsBaseOptions(argsman);

    argsman.AddArg("-acceptnonstdtxn", strprintf("Relay and mine \"non-standard\" transactions (%sdefault: %u)", "testnet/regtest only; ", !testnetChainParams->RequireStandard()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-incrementalrelayfee=<amt>", strprintf("Fee rate (in %s/kvB) used to define cost of relay, used for mempool limiting and BIP 125 replacement. (default: %s)", CURRENCY_UNIT, FormatMoney(DEFAULT_INCREMENTAL_RELAY_FEE)), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-dustrelayfee=<amt>", strprintf("Fee rate (in %s/kvB) used to define dust, the value of an output such that it will cost more than its value in fees at this fee rate to spend it. (default: %s)", CURRENCY_UNIT, FormatMoney(DUST_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-bytespersigop", strprintf("Equivalent bytes per sigop in transactions for relay and mining (default: %u)", DEFAULT_BYTES_PER_SIGOP), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-datacarrier", strprintf("Relay and mine data carrier transactions (default: %u)", DEFAULT_ACCEPT_DATACARRIER), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-datacarriersize", strprintf("Maximum size of data in data carrier transactions we relay and mine (default: %u)", MAX_OP_RETURN_RELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-minrelaytxfee=<amt>", strprintf("Fees (in %s/kvB) smaller than this are considered zero fee for relaying, mining and transaction creation (default: %s)",
        CURRENCY_UNIT, FormatMoney(DEFAULT_MIN_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-whitelistforcerelay", strprintf("Add 'forcerelay' permission to whitelisted inbound peers with default permissions. This will relay transactions even if the transactions were already in the mempool. (default: %d)", DEFAULT_WHITELISTFORCERELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-whitelistrelay", strprintf("Add 'relay' permission to whitelisted inbound peers with default permissions. This will accept relayed transactions even when not relaying transactions (default: %d)", DEFAULT_WHITELISTRELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);


    argsman.AddArg("-blockmaxweight=<n>", strprintf("Set maximum BIP141 block weight (default: %d)", DEFAULT_BLOCK_MAX_WEIGHT), ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-blockmintxfee=<amt>", strprintf("Set lowest fee rate (in %s/kvB) for transactions to be included in block creation. (default: %s)", CURRENCY_UNIT, FormatMoney(DEFAULT_BLOCK_MIN_TX_FEE)), ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-blockversion=<n>", "Override block version to test forking scenarios", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::BLOCK_CREATION);

    argsman.AddArg("-rest", strprintf("Accept public REST requests (default: %u)", DEFAULT_REST_ENABLE), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcallowip=<ip>", "Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcauth=<userpw>", "Username and HMAC-SHA-256 hashed password for JSON-RPC connections. The field <userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical python script is included in share/rpcauth. The client then connects normally using the rpcuser=<USERNAME>/rpcpassword=<PASSWORD> pair of arguments. This option can be specified multiple times", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-rpcbind=<addr>[:port]", "Bind to given address to listen for JSON-RPC connections. Do not expose the RPC server to untrusted networks such as the public internet! This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -rpcport. Use [host]:port notation for IPv6. This option can be specified multiple times (default: 127.0.0.1 and ::1 i.e., localhost)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-rpccookiefile=<loc>", "Location of the auth cookie. Relative paths will be prefixed by a net-specific datadir location. (default: data dir)", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcpassword=<pw>", "Password for JSON-RPC connections", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
   