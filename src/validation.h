// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_H
#define BITCOIN_VALIDATION_H

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <arith_uint256.h>
#include <attributes.h>
#include <chain.h>
#include <consensus/amount.h>
#include <fs.h>
#include <node/blockstorage.h>
#include <policy/feerate.h>
#include <policy/packages.h>
#include <script/script_error.h>
#include <sync.h>
#include <txdb.h>
#include <txmempool.h> // For CTxMemPool::cs
#include <uint256.h>
#include <util/check.h>
#include <util/hasher.h>
#include <util/translation.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdint.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class CChainState;
class CBlockTreeDB;
class CChainParams;
class CTxMemPool;
class ChainstateManager;
class SnapshotMetadata;
struct ChainTxData;
struct DisconnectedBlockTransactions;
struct PrecomputedTransactionData;
struct LockPoints;
struct AssumeutxoData;

/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const unsigned int DEFAULT_MIN_RELAY_TX_FEE = 1000;
/** Default for -limitancestorcount, max number of in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_LIMIT = 25;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_SIZE_LIMIT = 101;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_LIMIT = 25;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_SIZE_LIMIT = 101;

// If a package is submitted, it must be within the mempool's ancestor/descendant limits. Since a
// submitted package must be child-with-unconfirmed-parents (all of the transactions are an ancestor
// of the child), package limits are ultimately bounded by mempool package limits. Ensure that the
// defaults reflect this constraint.
static_assert(DEFAULT_DESCENDANT_LIMIT >= MAX_PACKAGE_COUNT);
static_assert(DEFAULT_ANCESTOR_LIMIT >= MAX_PACKAGE_COUNT);
static_assert(DEFAULT_ANCESTOR_SIZE_LIMIT >= MAX_PACKAGE_SIZE);
static_assert(DEFAULT_DESCENDANT_SIZE_LIMIT >= MAX_PACKAGE_SIZE);

/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
static const unsigned int DEFAULT_MEMPOOL_EXPIRY = 336;
/** Maximum number of dedicated script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 15;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
static const int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;
static const bool DEFAULT_CHECKPOINTS_ENABLED = true;
static const bool DEFAULT_TXINDEX = false;
static constexpr bool DEFAULT_COINSTATSINDEX{false};
static const char* const DEFAULT_BLOCKFILTERINDEX = "0";
/** Default for -persistmempool */
static const bool DEFAULT_PERSIST_MEMPOOL = true;
/** Default for -stopatheight */
static const int DEFAULT_STOPATHEIGHT = 0;
/** Block files containing a block-height within MIN_BLOCKS_TO_KEEP of ActiveChain().Tip() will not be pruned. */
static const unsigned int MIN_BLOCKS_TO_KEEP = 288;
static const signed int DEFAULT_CHECKBLOCKS = 6;
static const unsigned int DEFAULT_CHECKLEVEL = 3;
// Require that user allocate at least 550 MiB for block & undo files (blk???.dat and rev???.dat)
// At 1MB per block, 288 blocks = 288MB.
// Add 15% for Undo data = 331MB
// Add 20% for Orphan block rate = 397MB
// We want the low water mark after pruning to be at least 397 MB and since we prune in
// full block file chunks, we need the high water mark which triggers the prune to be
// one 128MB block file + added 15% undo data = 147MB greater for a total of 545MB
// Setting the target to >= 550 MiB will make it likely we can respect the target.
static const uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 550 * 1024 * 1024;

/** Current sync state passed to tip changed callbacks. */
enum class SynchronizationState {
    INIT_REINDEX,
    INIT_DOWNLOAD,
    POST_INIT
};

extern RecursiveMutex cs_main;
extern Mutex g_best_block_mutex;
extern std::condition_variable g_best_block_cv;
/** Used to notify getblocktemplate RPC of new tips. */
extern uint256 g_best_block;
/** Whether there are dedicated script-checking threads running.
 * False indicates all script checking is done on the main threadMessageHandler thread.
 */
extern bool g_parallel_script_checks;
extern bool fRequireStandard;
extern bool fCheckBlockIndex;
extern bool fCheckpointsEnabled;
/** A fee rate smaller than this is considered zero fee (for relaying, mining and transaction creation) */
extern CFeeRate minRelayTxFee;
/** If the tip is older than this (in seconds), the node is considered to be in initial block download. */
extern int64_t nMaxTipAge;

/** Block hash whose ancestors we will assume to have valid scripts without checking them. */
extern uint256 hashAssumeValid;

/** Minimum work we will assume exists on some valid chain. */
extern arith_uint256 nMinimumChainWork;

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex *pindexBestHeader;

/** Documentation for argument 'checklevel'. */
extern const std::vector<std::string> CHECKLEVEL_DOC;

/** Unload database information */
void UnloadBlockIndex(CTxMemPool* mempoo