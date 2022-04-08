// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <checkqueue.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_check.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <cuckoocache.h>
#include <deploymentstatus.h>
#include <flatfile.h>
#include <hash.h>
#include <index/blockfilterindex.h>
#include <logging.h>
#include <logging/timer.h>
#include <node/blockstorage.h>
#include <node/coinstats.h>
#include <node/ui_interface.h>
#include <node/utxo_snapshot.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <policy/settings.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <random.h>
#include <reverse_iterator.h>
#include <script/script.h>
#include <script/sigcache.h>
#include <shutdown.h>
#include <signet.h>
#include <timedata.h>
#include <tinyformat.h>
#include <txdb.h>
#include <txmempool.h>
#include <uint256.h>
#include <undo.h>
#include <util/check.h> // For NDEBUG compile time check
#include <util/hasher.h>
#include <util/moneystr.h>
#include <util/rbf.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/trace.h>
#include <util/translation.h>
#include <validationinterface.h>
#include <warnings.h>

#include <algorithm>
#include <numeric>
#include <optional>
#include <string>

#include <boost/algorithm/string/replace.hpp>

#define MICRO 0.000001
#define MILLI 0.001

/**
 * An extra transaction can be added to a package, as long as it only has one
 * ancestor and is no larger than this. Not really any reason to make this
 * configurable as it doesn't materially change DoS parameters.
 */
static const unsigned int EXTRA_DESCENDANT_TX_SIZE_LIMIT = 10000;
/** Maximum kilobytes for transactions to store for processing during reorg */
static const unsigned int MAX_DISCONNECTED_TX_POOL_SIZE = 20000;
/** Time to wait between writing blocks/block index to disk. */
static constexpr std::chrono::hours DATABASE_WRITE_INTERVAL{1};
/** Time to wait between flushing chainstate to disk. */
static constexpr std::chrono::hours DATABASE_FLUSH_INTERVAL{24};
/** Maximum age of our tip for us to be considered current for fee estimation */
static constexpr std::chrono::hours MAX_FEE_ESTIMATION_TIP_AGE{3};
const std::vector<std::string> CHECKLEVEL_DOC {
    "level 0 reads the blocks from disk",
    "level 1 verifies block validity",
    "level 2 verifies undo data",
    "level 3 checks disconnection of tip blocks",
    "level 4 tries to reconnect the blocks",
    "each level includes the checks of the previous levels",
};

bool CBlockIndexWorkComparator::operator()(const CBlockIndex *pa, const CBlockIndex *pb) const {
    // First sort by most total work, ...
    if (pa->nChainWork > pb->nChainWork) return false;
    if (pa->nChainWork < pb->nChainWork) return true;

    // ... then by earliest time received, ...
    if (pa->nSequenceId < pb->nSequenceId) return false;
    if (pa->nSequenceId > pb->nSequenceId) return true;

    // Use pointer address as tie breaker (should only happen with blocks
    // loaded from disk, as those all have id 0).
    if (pa < pb) return false;
    if (pa > pb) return true;

    // Identical blocks.
    return false;
}

/**
 * Mutex to guard access to validation specific variables, such as reading
 * or changing the chainstate.
 *
 * This may also need to be locked when updating the transaction pool, e.g. on
 * AcceptToMemoryPool. See CTxMemPool::cs comment for details.
 *
 * The transaction pool has a separate lock to allow reading from it and the
 * chainstate at the same time.
 */
RecursiveMutex cs_main;

CBlockIndex *pindexBestHeader = nullptr;
Mutex g_best_block_mutex;
std::condition_variable g_best_block_cv;
uint256 g_best_block;
bool g_parallel_script_checks{false};
bool fRequireStandard = true;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

uint256 hashAssumeValid;
arith_uint256 nMinimumChainWork;

CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);

CBlockIndex* CChainState::FindForkInGlobalIndex(const CBlockLocator& locator) const
{
    AssertLockHeld(cs_main);

    // Find the latest block common to locator and chain - we expect that
    // locator.vHave is sorted descending by height.
    for (const uint256& hash : locator.vHave) {
        CBlockIndex* pindex{m_blockman.LookupBlockIndex(hash)};
        if (pindex) {
            if (m_chain.Contains(pindex)) {
                return pindex;
            }
            if (pindex->GetAncestor(m_chain.Height()) == m_chain.Tip()) {
                return m_chain.Tip();
            }
        }
    }
    return m_chain.Genesis();
}

bool CheckInputScripts(const CTransaction& tx, TxValidationState& state,
                       const CCoinsViewCache& inputs, unsigned int flags, bool cacheSigStore,
                       bool cacheFullScriptStore, PrecomputedTransactionData& txdata,
                       std::vector<CScriptCheck>* pvChecks = nullptr)
                       EXCLUSIVE_LOCKS_REQUIRED(cs_main);

bool CheckFinalTx(const CBlockIndex* active_chain_tip, const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);
    assert(active_chain_tip); // TODO: Make active_chain_tip a reference

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses active_chain_tip.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than active_chain_tip.Height().
    const int nBlockHeight = active_chain_tip->nHeight + 1;

    // BIP113 requires that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                             ? active_chain_tip->GetMedianTimePast()
                             : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

bool CheckSequenceLocks(CBlockIndex* tip,
                        const CCoinsView& coins_view,
                        const CTransaction& tx,
                        int flags,
                        LockPoints* lp,
                        bool useExistingLockPoints)
{
    assert(tip != nullptr);

    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses active_chainstate.m_chain.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than active_chainstate.m_chain.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else {
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn& txin = tx.vin[txinIndex];
            Coin coin;
            if (!coins_view.GetCoin(txin.prevout, coin)) {
                return error("%s: Missing input", __func__);
            }
            if (coin.nHeight == MEMPOOL_HEIGHT) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                prevheights[txinIndex] = coin.nHeight;
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, prevheights, index);
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for (const int height : prevheights) {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight+1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}

// Returns the script flags which should be checked for a given block
static unsigned int GetBlockScriptFlags(const CBlockIndex* pindex, const Consensus::Params& chainparams);

static void LimitMempoolSize(CTxMemPool& pool, CCoinsViewCache& coins_cache, size_t limit, std::chrono::seconds age)
    EXCLUSIVE_LOCKS_REQUIRED(pool.cs, ::cs_main)
{
    int expired = pool.Expire(GetTime<std::chrono::seconds>() - age);
    if (expired != 0) {
        LogPrint(BCLog::MEMPOOL, "Expired %i transactions from the memory pool\n", expired);
    }

    std::vector<COutPoint> vNoSpendsRemaining;
    pool.TrimToSize(limit, &vNoSpendsRemaining);
    for (const COutPoint& removed : vNoSpendsRemaining)
        coins_cache.Uncache(removed);
}

static bool IsCurrentForFeeEstimation(CChainState& active_chainstate) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);
    if (active_chainstate.IsInitialBlockDownload())
        return false;
    if (active_chainstate.m_chain.Tip()->GetBlockTime() < count_seconds(GetTime<std::chrono::seconds>() - MAX_FEE_ESTIMATION_TIP_AGE))
        return false;
    if (active_chainstate.m_chain.Height() < pindexBestHeader->nHeight - 1)
        return false;
    return true;
}

void CChainState::MaybeUpdateMempoolForReorg(
    DisconnectedBlockTransactions& disconnectpool,
    bool fAddToMempool)
{
    if (!m_mempool) return;

    AssertLockHeld(cs_main);
    AssertLockHeld(m_mempool->cs);
    std::vector<uint256> vHashUpdate;
    // disconnectpool's insertion_order index sorts the entries from
    // oldest to newest, but the oldest entry will be the last tx from the
    // latest mined block that was disconnected.
    // Iterate disconnectpool in reverse, so that we add transactions
    // back to the mempool starting with the earliest transaction that had
    // been previously seen in a block.
    auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
    while (it != disconnectpool.queuedTx.get<insertion_order>().rend()) {
        // ignore validation errors in resurrected transactions
        if (!fAddToMempool || (*it)->IsCoinBase() ||
            AcceptToMemoryPool(*this, *it, GetTime(),
                /*bypass_limits=*/true, /*test_accept=*/false).m_result_type !=
                    MempoolAcceptResult::ResultType::VALID) {
            // If the transaction doesn't make it in to the mempool, remove any
            // transactions that depend on it (which would now be orphans).
            m_mempool->removeRecursive(**it, MemPoolRemovalReason::REORG);
        } else if (m_mempool->exists(GenTxid::Txid((*it)->GetHash()))) {
            vHashUpdate.push_back((*it)->GetHash());
        }
        ++it;
    }
    disconnectpool.queuedTx.clear();
    // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
    // no in-mempool children, which is generally not true wh