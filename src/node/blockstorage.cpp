// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/blockstorage.h>

#include <chain.h>
#include <chainparams.h>
#include <clientversion.h>
#include <consensus/validation.h>
#include <flatfile.h>
#include <fs.h>
#include <hash.h>
#include <pow.h>
#include <reverse_iterator.h>
#include <shutdown.h>
#include <signet.h>
#include <streams.h>
#include <undo.h>
#include <util/syscall_sandbox.h>
#include <util/system.h>
#include <validation.h>

std::atomic_bool fImporting(false);
std::atomic_bool fReindex(false);
bool fHavePruned = false;
bool fPruneMode = false;
uint64_t nPruneTarget = 0;

static FILE* OpenUndoFile(const FlatFilePos& pos, bool fReadOnly = false);
static FlatFileSeq BlockFileSeq();
static FlatFileSeq UndoFileSeq();

CBlockIndex* BlockManager::LookupBlockIndex(const uint256& hash) const
{
    AssertLockHeld(cs_main);
    BlockMap::const_iterator it = m_block_index.find(hash);
    return it == m_block_index.end() ? nullptr : it->second;
}

CBlockIndex* BlockManager::AddToBlockIndex(const CBlockHeader& block)
{
    AssertLockHeld(cs_main);

    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = m_block_index.find(hash);
    if (it != m_block_index.end()) {
        return it->second;
    }

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = m_block_index.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = m_block_index.find(block.hashPrevBlock);
    if (miPrev != m_block_index.end()) {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nTimeMax = (pindexNew->pprev ? std::max(pindexNew->pprev->nTimeMax, pindexNew->nTime) : pindexNew->nTime);
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == nullptr || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    m_dirty_blockindex.insert(pindexNew);

    return pindexNew;
}

void BlockManager::PruneOneBlockFile(const int fileNumber)
{
    AssertLockHeld(cs_main);
    LOCK(cs_LastBlockFile);

    for (const auto& entry : m_block_index) {
        CBlockIndex* pindex = entry.second;
        if (pindex->nFile == fileNumber) {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            m_dirty_blockindex.insert(pindex);

            // Prune from m_blocks_unlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // m_blocks_unlinked or setBlockIndexCandidates.
            auto range = m_blocks_unlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator _it = range.first;
                range.first++;
                if (_it->second == pindex) {
                    m_blocks_unlinked.erase(_it);
                }
            }
        }
    }

    m_blockfile_info[fileNumber].SetNull();
    m_dirty_fileinfo.insert(fileNumber);
}

void BlockManager::FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight, int chain_tip_height)
{
    assert(fPruneMode && nManualPruneHeight > 0);

    LOCK2(cs_main, cs_LastBlockFile);
    if (chain_tip_height < 0) {
        return;
    }

    // last block to prune is the lesser of (user-specified height, MIN_BLOCKS_TO_KEEP from the tip)
    unsigned int nLastBlockWeCanPrune = std::min((unsigned)nManualPruneHeight, chain_tip_height - MIN_BLOCKS_TO_KEEP);
    int count = 0;
    for (int fileNumber = 0; fileNumber < m_last_blockfile; fileNumber++) {
        if (m_blockfile_info[fileNumber].nSize == 0 || m_blockfile_info[fileNumber].nHeightLast > nLastBlockWeCanPrune) {
            continue;
        }
        PruneOneBlockFile(fileNumber);
        setFilesToPrune.insert(fileNumber);
        count++;
    }
    LogPrintf("Prune (Manual): prune_height=%d removed %d blk/rev pairs\n", nLastBlockWeCanPrune, count);
}

void BlockManager::FindFilesToPrune(std::set<int>& setFilesToPrune, uint64_t nPruneAfterHeight, int chain_tip_height, int prune_height, bool is_ibd)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chain_tip_height < 0 || nPruneTarget == 0) {
        return;
    }
    if ((uint64_t)chain_tip_height <= nPruneAfterHeight) {
        return;
    }

    unsigned int nLastBlockWeCanPrune{(unsigned)std::min(prune_height, chain_tip_height - static_cast<int>(MIN_BLOCKS_TO_KEEP))};
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count = 0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        // On a prune event, the chainstate DB is flushed.
        // To avoid excessive prune events negating the benefit of high dbcache
        // values, we should not prune too rapidly.
        // So when pruning in IBD, increase the buffer a bit to avoid a re-prune too soon.
        if (is_ibd) {
            // Since this is only relevant during IBD, we use a fixed 10%
            nBuffer += nPruneTarget / 10;
        }

        for (int fileNumber = 0; fileNumber < m_last_blockfile; fileNumber++) {
            nBytesToPrune = m_blockfile_info[fileNumber].nSize + m_blockfile_info[fileNumber].nUndoSize;

            if (m_blockfile_info[fileNumber].nSize == 0) {
                continue;
            }

            if (nCurrentUsage + nBuffer < nPruneTarget) { // are we below our target?
                break;
            }

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (m_blockfile_info[fileNumber].nHeightLast > nLastBlockWeCanPrune) {
                continue;
            }

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint(BCLog::PRUNE, "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

CBlockIndex* BlockManager::InsertBlockIndex(const uint256& hash)
{
    AssertLockHeld(cs_main);

    if (hash.IsNull()) {
        return nullptr;
    }

    // Return existing
    BlockMap::iterator mi = m_block_index.find(hash);
    if (mi != m_block_index.end()) {
        return (*mi).second;
    }

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    mi = m_block_index.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool BlockManager::LoadBlockIndex(
    const Consensus::Params& consensus_params,
    ChainstateManager& chainman)
{
    if (!m_block_tree_db->LoadBlockIndexGuts(consensus_params, [this](const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return this->InsertBlockIndex(hash); })) {
        return false;
    }

    // Calculate nChainWork
    std::vector<std::pair<int, CBlockIndex*>> vSortedByHeight;
    vSortedByHeight.reserve(m_block_index.size());
    for (const std::pair<const uint256, CBlockIndex*>& item : m_block_index) {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(std::make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());

    // Find start of assumed-valid region.
    int first_assumed_valid_height = std::numeric_limits<int>::max();

    for (const auto& [height, block] : vSortedByHeight) {
        if (block->IsAssumedValid()) {
            auto chainstates = chainman.GetAll();

            // If we encounter an assumed-valid block index entry, ensure that we have
            // one chainstate that tolerates assumed-valid entries and another that does
            // not (i.e. the background validation chainstate), since assumed-valid
            // entries should always be pending validation by a fully-validated chainstate.
            auto any_chain = [&](auto fnc) { return std::any_of(chainstates.cbegin(), chainstates.cend(), fnc); };
            assert(any_chain([](auto chainstate) { return chainstate->reliesOnAssumedValid(); }));
            assert(any_chain([](auto chainstate) { return !chainstate->reliesOnAssumedValid(); }));

            first_assumed_valid_height = height;
            break;
        }
    }

    for (const std::pair<int, CBlockIndex*>& item : vSortedByHeight) {
        if (ShutdownRequested()) return false;
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        pindex->nTimeMax = (pindex->pprev ? std::max(pindex->pprev->nTimeMax, pindex->nTime) : pindex->nTime);

        // We can link the chain of blocks for which we've received transactions at some point, or
        // blocks that are assumed-valid on the basis of snapshot load (see
        // PopulateAndValidateSnapshot()).
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx > 0) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    m_blocks_unlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (!(pindex->nStatus & BLOCK_FAILED_MASK) && pindex->pprev && (pindex->pprev->nStatus & BLOCK_FAILED_MASK)) {
            pindex->nStatus |= BLOCK_FAILED_CHILD;
            m_dirty_blockindex.insert(pindex);
        }
        if (pindex->IsAssumedValid() ||
                (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) &&
                 (pindex->HaveTxsDownloaded() || pindex->pprev == nullptr))) {

            // Fill each chainstate's block candidate set. Only add assumed-valid
            // blocks to the tip candidate set if the chainstate is allowed to rely on
            // assumed-valid blocks.
            //
            // If all setBlockIndexCandidates contained the assumed-valid blocks, the
            // background chainstate's ActivateBestChain() call would add assumed-valid
            // blocks to the chain (based on how FindMostWorkChain() works). Obviously
            // we don't want this since the purpose of the background validation chain
            // is to validate assued-valid blocks.
            //
            // Note: This is considering all blocks whose height is greater or equal to
            // the first assumed-valid block to be assumed-valid blocks, and excluding
            // them from the background chainstate's setBlockIndexCandidates set. This
            // does mean that some blocks which are not technically assumed-valid
            // (later blocks on a fork beginning before the first assumed-valid block)
            // might not get added to the the background chainstate, but this is ok,
            // because they will still be attached to the active chainstate if they
            // actually contain more work.
            //
            // Instead of this height-based approach, an earlier attempt was made at
            // detecting "holistically" whether the block index under consideration
            // relied on an assumed-valid ancestor, but this proved to be too slow to
            // be practical.
            for (CChainState* chainstate : chainman.GetAll()) {
                if (chainstate->reliesOnAssumedValid() ||
                        pindex->nHeight < first_assumed_valid_height) {
                    chainstate->setBlockIndexCandidates.insert(pindex);
                }
            }
        }
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!chainman.m_best_invalid || pindex->nChainWork > chainman.m_best_invalid->nChainWork)) {
            chainman.m_best_invalid = pindex;
        }
        if (pindex->pprev) {
            pindex->BuildSkip();
        }
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == nullptr || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    return true;
}

void BlockManager::Unload()
{
    m_blocks_unlinked.clear();

    for (const BlockMap::value_type& entry : m_block_index) {
        delete entry.second;
    }

    m_block_index.clear();

    m_blockfile_info.clear();
    m_last_blockfile = 0;
    m_dirty_blockindex.clear();
    m_dirty_fileinfo.clear();
}

bool BlockManager::WriteBlockIndexDB()
{
    AssertLockHeld(::cs_main);
    std::vector<std::pair<int, const CBlockFileInfo*>> vFiles;
    vFiles.reserve(m_dirty_fileinfo.size());
    for (std::set<int>::iterator it = m_dirty_fileinfo.begin(); it != m_dirty_fileinfo.end();) {
        vFiles.push_back(std::make_pair(*it, &m_blockfile_info[*it]));
        m_dirty_fileinfo.erase(it++);
    }
    std::vector<const CBlockIndex*> vBlocks;
    vBlocks.reserve(m_dirty_blockindex.size());
    for (std::set<CBlockIndex*>::iterator it = m_dirty_blockindex.begin(); it != m_dirty_blockindex.end();) {
        vBlocks.push_back(*it);
        m_dirty_blockindex.erase(it++);
    }
    if (!m_block_tree_db->WriteBatchSync(vFiles, m_last_blockfile, vBlocks)) {
       