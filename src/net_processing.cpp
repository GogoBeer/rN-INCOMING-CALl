// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net_processing.h>

#include <addrman.h>
#include <banman.h>
#include <blockencodings.h>
#include <blockfilter.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <hash.h>
#include <index/blockfilterindex.h>
#include <merkleblock.h>
#include <netbase.h>
#include <netmessagemaker.h>
#include <node/blockstorage.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <random.h>
#include <reverse_iterator.h>
#include <scheduler.h>
#include <streams.h>
#include <sync.h>
#include <tinyformat.h>
#include <txmempool.h>
#include <txorphanage.h>
#include <txrequest.h>
#include <util/check.h> // For NDEBUG compile time check
#include <util/strencodings.h>
#include <util/system.h>
#include <util/trace.h>
#include <validation.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <typeinfo>

/** How long to cache transactions in mapRelay for normal relay */
static constexpr auto RELAY_TX_CACHE_TIME = 15min;
/** How long a transaction has to be in the mempool before it can unconditionally be relayed (even when not in mapRelay). */
static constexpr auto UNCONDITIONAL_RELAY_DELAY = 2min;
/** Headers download timeout.
 *  Timeout = base + per_header * (expected number of headers) */
static constexpr auto HEADERS_DOWNLOAD_TIMEOUT_BASE = 15min;
static constexpr auto HEADERS_DOWNLOAD_TIMEOUT_PER_HEADER = 1ms;
/** Protect at least this many outbound peers from disconnection due to slow/
 * behind headers chain.
 */
static constexpr int32_t MAX_OUTBOUND_PEERS_TO_PROTECT_FROM_DISCONNECT = 4;
/** Timeout for (unprotected) outbound peers to sync to our chainwork */
static constexpr auto CHAIN_SYNC_TIMEOUT{20min};
/** How frequently to check for stale tips */
static constexpr auto STALE_CHECK_INTERVAL{10min};
/** How frequently to check for extra outbound peers and disconnect */
static constexpr auto EXTRA_PEER_CHECK_INTERVAL{45s};
/** Minimum time an outbound-peer-eviction candidate must be connected for, in order to evict */
static constexpr auto MINIMUM_CONNECT_TIME{30s};
/** SHA256("main address relay")[0:8] */
static constexpr uint64_t RANDOMIZER_ID_ADDRESS_RELAY = 0x3cac0035b5866b90ULL;
/// Age after which a stale block will no longer be served if requested as
/// protection against fingerprinting. Set to one month, denominated in seconds.
static constexpr int STALE_RELAY_AGE_LIMIT = 30 * 24 * 60 * 60;
/// Age after which a block is considered historical for purposes of rate
/// limiting block relay. Set to one week, denominated in seconds.
static constexpr int HISTORICAL_BLOCK_AGE = 7 * 24 * 60 * 60;
/** Time between pings automatically sent out for latency probing and keepalive */
static constexpr auto PING_INTERVAL{2min};
/** The maximum number of entries in a locator */
static const unsigned int MAX_LOCATOR_SZ = 101;
/** The maximum number of entries in an 'inv' protocol message */
static const unsigned int MAX_INV_SZ = 50000;
/** Maximum number of in-flight transaction requests from a peer. It is not a hard limit, but the threshold at which
 *  point the OVERLOADED_PEER_TX_DELAY kicks in. */
static constexpr int32_t MAX_PEER_TX_REQUEST_IN_FLIGHT = 100;
/** Maximum number of transactions to consider for requesting, per peer. It provides a reasonable DoS limit to
 *  per-peer memory usage spent on announcements, while covering peers continuously sending INVs at the maximum
 *  rate (by our own policy, see INVENTORY_BROADCAST_PER_SECOND) for several minutes, while not receiving
 *  the actual transaction (from any peer) in response to requests for them. */
static constexpr int32_t MAX_PEER_TX_ANNOUNCEMENTS = 5000;
/** How long to delay requesting transactions via txids, if we have wtxid-relaying peers */
static constexpr auto TXID_RELAY_DELAY{2s};
/** How long to delay requesting transactions from non-preferred peers */
static constexpr auto NONPREF_PEER_TX_DELAY{2s};
/** How long to delay requesting transactions from overloaded peers (see MAX_PEER_TX_REQUEST_IN_FLIGHT). */
static constexpr auto OVERLOADED_PEER_TX_DELAY{2s};
/** How long to wait before downloading a transaction from an additional peer */
static constexpr auto GETDATA_TX_INTERVAL{60s};
/** Limit to avoid sending big packets. Not used in processing incoming GETDATA for compatibility */
static const unsigned int MAX_GETDATA_SZ = 1000;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Time during which a peer must stall block download progress before being disconnected. */
static constexpr auto BLOCK_STALLING_TIMEOUT{2s};
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached its tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
/** Maximum depth of blocks we're willing to serve as compact blocks to peers
 *  when requested. For older blocks, a regular BLOCK response will be sent. */
static const int MAX_CMPCTBLOCK_DEPTH = 5;
/** Maximum depth of blocks we're willing to respond to GETBLOCKTXN requests for. */
static const int MAX_BLOCKTXN_DEPTH = 10;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and pruning harder). We'll probably
 *  want to make this a per-peer adaptive value at some point. */
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Block download timeout base, expressed in multiples of the block interval (i.e. 10 min) */
static constexpr double BLOCK_DOWNLOAD_TIMEOUT_BASE = 1;
/** Additional block download timeout per parallel downloading peer (i.e. 5 min) */
static constexpr double BLOCK_DOWNLOAD_TIMEOUT_PER_PEER = 0.5;
/** Maximum number of headers to announce when relaying blocks with headers message.*/
static const unsigned int MAX_BLOCKS_TO_ANNOUNCE = 8;
/** Maximum number of unconnecting headers announcements before DoS score */
static const int MAX_UNCONNECTING_HEADERS = 10;
/** Minimum blocks required to signal NODE_NETWORK_LIMITED */
static const unsigned int NODE_NETWORK_LIMITED_MIN_BLOCKS = 288;
/** Average delay between local address broadcasts */
static constexpr auto AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL{24h};
/** Average delay between peer address broadcasts */
static constexpr auto AVG_ADDRESS_BROADCAST_INTERVAL{30s};
/** Average delay between trickled inventory transmissions for inbound peers.
 *  Blocks and peers with NetPermissionFlags::NoBan permission bypass this. */
static constexpr auto INBOUND_INVENTORY_BROADCAST_INTERVAL{5s};
/** Average delay between trickled inventory transmissions for outbound peers.
 *  Use a smaller delay as there is less privacy concern for them.
 *  Blocks and peers with NetPermissionFlags::NoBan permission bypass this. */
static constexpr auto OUTBOUND_INVENTORY_BROADCAST_INTERVAL{2s};
/** Maximum rate of inventory items to send per second.
 *  Limits the impact of low-fee transaction floods. */
static constexpr unsigned int INVENTORY_BROADCAST_PER_SECOND = 7;
/** Maximum number of inventory items to send per transmission. */
static constexpr unsigned int INVENTORY_BROADCAST_MAX = INVENTORY_BROADCAST_PER_SECOND * count_seconds(INBOUND_INVENTORY_BROADCAST_INTERVAL);
/** The number of most recently announced transactions a peer can request. */
static constexpr unsigned int INVENTORY_MAX_RECENT_RELAY = 3500;
/** Verify that INVENTORY_MAX_RECENT_RELAY is enough to cache everything typically
 *  relayed before unconditional relay from the mempool kicks in. This is only a
 *  lower bound, and it should be larger to account for higher inv rate to outbound
 *  peers, and random variations in the broadcast mechanism. */
static_assert(INVENTORY_MAX_RECENT_RELAY >= INVENTORY_BROADCAST_PER_SECOND * UNCONDITIONAL_RELAY_DELAY / std::chrono::seconds{1}, "INVENTORY_RELAY_MAX too low");
/** Average delay between feefilter broadcasts in seconds. */
static constexpr auto AVG_FEEFILTER_BROADCAST_INTERVAL{10min};
/** Maximum feefilter broadcast delay after significant change. */
static constexpr auto MAX_FEEFILTER_CHANGE_DELAY{5min};
/** Maximum number of compact filters that may be requested with one getcfilters. See BIP 157. */
static constexpr uint32_t MAX_GETCFILTERS_SIZE = 1000;
/** Maximum number of cf hashes that may be requested with one getcfheaders. See BIP 157. */
static constexpr uint32_t MAX_GETCFHEADERS_SIZE = 2000;
/** the maximum percentage of addresses from our addrman to return in response to a getaddr message. */
static constexpr size_t MAX_PCT_ADDR_TO_SEND = 23;
/** The maximum number of address records permitted in an ADDR message. */
static constexpr size_t MAX_ADDR_TO_SEND{1000};
/** The maximum rate of address records we're willing to process on average. Can be bypassed using
 *  the NetPermissionFlags::Addr permission. */
static constexpr double MAX_ADDR_RATE_PER_SECOND{0.1};
/** The soft limit of the address processing token bucket (the regular MAX_ADDR_RATE_PER_SECOND
 *  based increments won't go above this, but the MAX_ADDR_TO_SEND increment following GETADDR
 *  is exempt from this limit). */
static constexpr size_t MAX_ADDR_PROCESSING_TOKEN_BUCKET{MAX_ADDR_TO_SEND};

// Internal stuff
namespace {
/** Blocks that are in flight, and that are in the queue to be downloaded. */
struct QueuedBlock {
    /** BlockIndex. We must have this since we only request blocks when we've already validated the header. */
    const CBlockIndex* pindex;
    /** Optional, used for CMPCTBLOCK downloads */
    std::unique_ptr<PartiallyDownloadedBlock> partialBlock;
};

/**
 * Data structure for an individual peer. This struct is not protected by
 * cs_main since it does not contain validation-critical data.
 *
 * Memory is owned by shared pointers and this object is destructed when
 * the refcount drops to zero.
 *
 * Mutexes inside this struct must not be held when locking m_peer_mutex.
 *
 * TODO: move most members from CNodeState to this structure.
 * TODO: move remaining application-layer data members from CNode to this structure.
 */
struct Peer {
    /** Same id as the CNode object for this peer */
    const NodeId m_id{0};

    /** Protects misbehavior data members */
    Mutex m_misbehavior_mutex;
    /** Accumulated misbehavior score for this peer */
    int m_misbehavior_score GUARDED_BY(m_misbehavior_mutex){0};
    /** Whether this peer should be disconnected and marked as discouraged (unless it has NetPermissionFlags::NoBan permission). */
    bool m_should_discourage GUARDED_BY(m_misbehavior_mutex){false};

    /** Protects block inventory data members */
    Mutex m_block_inv_mutex;
    /** List of blocks that we'll announce via an `inv` message.
     * There is no final sorting before sending, as they are always sent
     * immediately and in the order requested. */
    std::vector<uint256> m_blocks_for_inv_relay GUARDED_BY(m_block_inv_mutex);
    /** Unfiltered list of blocks that we'd like to announce via a `headers`
     * message. If we can't announce via a `headers` message, we'll fall back to
     * announcing via `inv`. */
    std::vector<uint256> m_blocks_for_headers_relay GUARDED_BY(m_block_inv_mutex);
    /** The final block hash that we sent in an `inv` message to this peer.
     * When the peer requests this block, we send an `inv` message to trigger
     * the peer to request the next sequence of block hashes.
     * Most peers use headers-first syncing, which doesn't use this mechanism */
    uint256 m_continuation_block GUARDED_BY(m_block_inv_mutex) {};

    /** This peer's reported block height when we connected */
    std::atomic<int> m_starting_height{-1};

    /** The pong reply we're expecting, or 0 if no pong expected. */
    std::atomic<uint64_t> m_ping_nonce_sent{0};
    /** When the last ping was sent, or 0 if no ping was ever sent */
    std::atomic<std::chrono::microseconds> m_ping_start{0us};
    /** Whether a ping has been requested by the user */
    std::atomic<bool> m_ping_queued{false};

    /** A vector of addresses to send to the peer, limited to MAX_ADDR_TO_SEND. */
    std::vector<CAddress> m_addrs_to_send;
    /** Probabilistic filter to track recent addr messages relayed with this
     *  peer. Used to avoid relaying redundant addresses to this peer.
     *
     *  We initialize this filter for outbound peers (other than
     *  block-relay-only connections) or when an inbound peer sends us an
     *  address related message (ADDR, ADDRV2, GETADDR).
     *
     *  Presence of this filter must correlate with m_addr_relay_enabled.
     **/
    std::unique_ptr<CRollingBloomFilter> m_addr_known;
    /** Whether we are participating in address relay with this connection.
     *
     *  We set this bool to true for outbound peers (other than
     *  block-relay-only connections), or when an inbound peer sends us an
     *  address related message (ADDR, ADDRV2, GETADDR).
     *
     *  We use this bool to decide whether a peer is eligible for gossiping
     *  addr messages. This avoids relaying to peers that are unlikely to
     *  forward them, effectively blackholing self announcements. Reasons
     *  peers might support addr relay on the link include that they connected
     *  to us as a block-relay-only peer or they are a light client.
     *
     *  This field must correlate with whether m_addr_known has been
     *  initialized.*/
    std::atomic_bool m_addr_relay_enabled{false};
    /** Whether a getaddr request to this peer is outstanding. */
    bool m_getaddr_sent{false};
    /** Guards address sending timers. */
    mutable Mutex m_addr_send_times_mutex;
    /** Time point to send the next ADDR message to this peer. */
    std::chrono::microseconds m_next_addr_send GUARDED_BY(m_addr_send_times_mutex){0};
    /** Time point to possibly re-announce our local address to this peer. */
    std::chrono::microseconds m_next_local_addr_send GUARDED_BY(m_addr_send_times_mutex){0};
    /** Whether the peer has signaled support for receiving ADDRv2 (BIP155)
     *  messages, indicating a preference to receive ADDRv2 instead of ADDR ones. */
    std::atomic_bool m_wants_addrv2{false};
    /** Whether this peer has already sent us a getaddr message. */
    bool m_getaddr_recvd{false};
    /** Number of addresses that can be processed from this peer. Start at 1 to
     *  permit self-announcement. */
    double m_addr_token_bucket{1.0};
    /** When m_addr_token_bucket was last updated */
    std::chrono::microseconds m_addr_token_timestamp{GetTime<std::chrono::microseconds>()};
    /** Total number of addresses that were dropped due to rate limiting. */
    std::atomic<uint64_t> m_addr_rate_limited{0};
    /** Total number of addresses that were processed (excludes rate-limited ones). */
    std::atomic<uint64_t> m_addr_processed{0};

    /** Set of txids to reconsider once their parent transactions have been accepted **/
    std::set<uint256> m_orphan_work_set GUARDED_BY(g_cs_orphans);

    /** Protects m_getdata_requests **/
    Mutex m_getdata_requests_mutex;
    /** Work queue of items requested by this peer **/
    std::deque<CInv> m_getdata_requests GUARDED_BY(m_getdata_requests_mutex);

    explicit Peer(NodeId id)
        : m_id(id)
    {}
};

using PeerRef = std::shared_ptr<Peer>;

class PeerManagerImpl final : public PeerManager
{
public:
    PeerManagerImpl(const CChainParams& chainparams, CConnman& connman, AddrMan& addrman,
                    BanMan* banman, ChainstateManager& chainman,
                    CTxMemPool& pool, bool ignore_incoming_txs);

    /** Overridden from CValidationInterface. */
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexConnected) override;
    void BlockDisconnected(const std::shared_ptr<const CBlock> &block, const CBlockIndex* pindex) override;
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;
    void BlockChecked(const CBlock& block, const BlockValidationState& state) override;
    void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock>& pblock) override;

    /** Implement NetEventsInterface */
    void InitializeNode(CNode* pnode) override;
    void FinalizeNode(const CNode& node) override;
    bool ProcessMessages(CNode* pfrom, std::atomic<bool>& interrupt) override;
    bool SendMessages(CNode* pto) override EXCLUSIVE_LOCKS_REQUIRED(pto->cs_sendProcessing);

    /** Implement PeerManager */
    void StartScheduledTasks(CScheduler& scheduler) override;
    void CheckForStaleTipAndEvictPeers() override;
    bool FetchBlock(NodeId id, const uint256& hash, const CBlockIndex& index) override;
    bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats) const override;
    bool IgnoresIncomingTxs() override { return m_ignore_incoming_txs; }
    void SendPings() override;
    void RelayTransaction(const uint256& txid, const uint256& wtxid) override;
    void SetBestHeight(int height) override { m_best_height = height; };
    void Misbehaving(const NodeId pnode, const int howmuch, const std::string& message) override;
    void ProcessMessage(CNode& pfrom, const std::string& msg_type, CDataStream& vRecv,
                        const std::chrono::microseconds time_received, const std::atomic<bool>& interruptMsgProc) override;

private:
    void _RelayTransaction(const uint256& txid, const uint256& wtxid)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Consider evicting an outbound peer based on the amount of time they've been behind our tip */
    void ConsiderEviction(CNode& pto, std::chrono::seconds time_in_seconds) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** If we have extra outbound peers, try to disconnect the one with the oldest block announcement */
    void EvictExtraOutboundPeers(std::chrono::seconds now) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Retrieve unbroadcast transactions from the mempool and reattempt sending to peers */
    void ReattemptInitialBroadcast(CScheduler& scheduler);

    /** Get a shared pointer to the Peer object.
     *  May return an empty shared_ptr if the Peer object can't be found. */
    PeerRef GetPeerRef(NodeId id) const;

    /** Get a shared pointer to the Peer object and remove it from m_peer_map.
     *  May return an empty shared_ptr if the Peer object can't be found. */
    PeerRef RemovePeer(NodeId id);

    /**
     * Potentially mark a node discouraged based on the contents of a BlockValidationState object
     *
     * @param[in] via_compact_block this bool is passed in because net_processing should
     * punish peers differently depending on whether the data was provided in a compact
     * block message or not. If the compact block had a valid header, but contained invalid
     * txs, the peer should not be punished. See BIP 152.
     *
     * @return Returns true if the peer was punished (probably disconnected)
     */
    bool MaybePunishNodeForBlock(NodeId nodeid, const BlockValidationState& state,
                                 bool via_compact_block, const std::string& message = "");

    /**
     * Potentially disconnect and discourage a node based on the contents of a TxValidationState object
     *
     * @return Returns true if the peer was punished (probably disconnected)
     */
    bool MaybePunishNodeForTx(NodeId nodeid, const TxValidationState& state, const std::string& message = "");

    /** Maybe disconnect a peer and discourage future connections from its address.
     *
     * @param[in]   pnode     The node to check.
     * @param[in]   peer      The peer object to check.
     * @return                True if the peer was marked for disconnection in this function
     */
    bool MaybeDiscourageAndDisconnect(CNode& pnode, Peer& peer);

    void ProcessOrphanTx(std::set<uint256>& orphan_work_set) EXCLUSIVE_LOCKS_REQUIRED(cs_main, g_cs_orphans);
    /** Process a single headers message from a peer. */
    void ProcessHeadersMessage(CNode& pfrom, const Peer& peer,
                               const std::vector<CBlockHeader>& headers,
                               bool via_compact_block);

    void SendBlockTransactions(CNode& pfrom, const CBlock& block, const BlockTransactionsRequest& req);

    /** Register with TxRequestTracker that an INV has been received from a
     *  peer. The announcement parameters are decided in PeerManager and then
     *  passed to TxRequestTracker. */
    void AddTxAnnouncement(const CNode& node, const GenTxid& gtxid, std::chrono::microseconds current_time)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    /** Send a version message to a peer */
    void PushNodeVersion(CNode& pnode);

    /** Send a ping message every PING_INTERVAL or if requested via RPC. May
     *  mark the peer to be disconnected if a ping has timed out.
     *  We use mockable time for ping timeouts, so setmocktime may cause pings
     *  to time out. */
    void MaybeSendPing(CNode& node_to, Peer& peer, std::chrono::microseconds now);

    /** Send `addr` messages on a regular schedule. */
    void MaybeSendAddr(CNode& node, Peer& peer, std::chrono::microseconds current_time);

    /** Relay (gossip) an address to a few randomly chosen nodes.
     *
     * @param[in] originator   The id of the peer that sent us the address. We don't want to relay it back.
     * @param[in] addr         Address to relay.
     * @param[in] fReachable   Whether the address' network is reachable. We relay unreachable
     *                         addresses less.
     */
    void RelayAddress(NodeId originator, const CAddress& addr, bool fReachable);

    /** Send `feefilter` message. */
    void MaybeSendFeefilter(CNode& node, std::chrono::microseconds current_time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    const CChainParams& m_chainparams;
    CConnman& m_connman;
    AddrMan& m_addrman;
    /** Pointer to this node's banman. May be nullptr - check existence before dereferencing. */
    BanMan* const m_banman;
    ChainstateManager& m_chainman;
    CTxMemPool& m_mempool;
    TxRequestTracker m_txrequest GUARDED_BY(::cs_main);

    /** The height of the best chain */
    std::atomic<int> m_best_height{-1};

    /** Next time to check for stale tip */
    std::chrono::seconds m_stale_tip_check_time{0s};

    /** Whether this node is running in blocks only mode */
    const bool m_ignore_incoming_txs;

    /** Whether we've completed initial sync yet, for determining when to turn
      * on extra block-relay-only peers. */
    bool m_initial_sync_finished{false};

    /** Protects m_peer_map. This mutex must not be locked while holding a lock
     *  on any of the mutexes inside a Peer object. */
    mutable Mutex m_peer_mutex;
    /**
     * Map of all Peer objects, keyed by peer id. This map is protected
     * by the m_peer_mutex. Once a shared pointer reference is
     * taken, the lock may be released. Individual fields are protected by
     * their own locks.
     */
    std::map<NodeId, PeerRef> m_peer_map GUARDED_BY(m_peer_mutex);

    /** Number of nodes with fSyncStarted. */
    int nSyncStarted GUARDED_BY(cs_main) = 0;

    /**
     * Sources of received blocks, saved to be able punish them when processing
     * happens afterwards.
     * Set mapBlockSource[hash].second to false if the node should not be
     * punished if the block is invalid.
     */
    std::map<uint256, std::pair<NodeId, bool>> mapBlockSource GUARDED_BY(cs_main);

    /** Number of peers with wtxid relay. */
    int m_wtxid_relay_peers GUARDED_BY(cs_main) = 0;

    /** Number of outbound peers with m_chain_sync.m_protect. */
    int m_outbound_peers_with_protect_from_disconnect GUARDED_BY(cs_main) = 0;

    bool AlreadyHaveTx(const GenTxid& gtxid) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Filter for transactions that were recently rejected by the mempool.
     * These are not rerequested until the chain tip changes, at which point
     * the entire filter is reset.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * We typically only add wtxids to this filter. For non-segwit
     * transactions, the txid == wtxid, so this only prevents us from
     * re-downloading non-segwit transactions when communicating with
     * non-wtxidrelay peers -- which is important for avoiding malleation
     * attacks that could otherwise interfere with transaction relay from
     * non-wtxidrelay peers. For communicating with wtxidrelay peers, having
     * the reject filter store wtxids is exactly what we want to avoid
     * redownload of a rejected transaction.
     *
     * In cases where we can tell that a segwit transaction will fail
     * validation no matter the witness, we may add the txid of such
     * transaction to the filter as well. This can be helpful when
     * communicating with txid-relay peers or if we were to otherwise fetch a
     * transaction via txid (eg in our orphan handling).
     *
     * Memory used: 1.3 MB
     */
    CRollingBloomFilter m_recent_rejects GUARDED_BY(::cs_main){120'000, 0.000'001};
    uint256 hashRecentRejectsChainTip GUARDED_BY(cs_main);

    /*
     * Filter for transactions that have been recently confirmed.
     * We use this to avoid requesting transactions that have already been
     * confirnmed.
     *
     * Blocks don't typically have more than 4000 transactions, so this should
     * be at least six blocks (~1 hr) worth of transactions that we can store,
     * inserting both a txid and wtxid for every observed transaction.
     * If the number of transactions appearing in a block goes up, or if we are
     * seeing getdata requests more than an hour after initial announcement, we
     * can increase this number.
     * The false positive rate of 1/1M should come out to less than 1
     * transaction per day that would be inadvertently ignored (which is the
     * same probability that we have in the reject filter).
     */
    Mutex m_recent_confirmed_transactions_mutex;
    CRollingBloomFilter m_recent_confirmed_transactions GUARDED_BY(m_recent_confirmed_transactions_mutex){48'000, 0.000'001};

    /** Have we requested this block from a peer */
    bool IsBlockRequested(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Remove this block from our tracked requested blocks. Called if:
     *  - the block has been received from a peer
     *  - the request for the block has timed out
     */
    void RemoveBlockRequest(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /* Mark a block as in flight
     * Returns false, still setting pit, if the block was already in flight from the same peer
     * pit will only be valid as long as the same cs_main lock is being held
     */
    bool BlockRequested(NodeId nodeid, const CBlockIndex& block, std::list<QueuedBlock>::iterator** pit = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool TipMayBeStale() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
     *  at most count entries.
     */
    void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<const CBlockIndex*>& vBlocks, NodeId& nodeStaller) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight GUARDED_BY(cs_main);

    /** When our tip was last updated. */
    std::atomic<std::chrono::seconds> m_last_tip_update{0s};

    /** Determine whether or not a peer can request a transaction, and return it (or nullptr if not found or not allowed). */
    CTransactionRef FindTxForGetData(const CNode& peer, const GenTxid& gtxid, const std::chrono::seconds mempool_req, const std::chrono::seconds now) LOCKS_EXCLUDED(cs_main);

    void ProcessGetData(CNode& pfrom, Peer& peer, const std::atomic<bool>& interruptMsgProc) EXCLUSIVE_LOCKS_REQUIRED(peer.m_getdata_requests_mutex) LOCKS_EXCLUDED(::cs_main);

    /** Process a new block. Perform any post-processing housekeeping */
    void ProcessBlock(CNode& node, const std::shared_ptr<const CBlock>& block, bool force_processing);

    /** Relay map (txid or wtxid -> CTransactionRef) */
    typedef std::map<uint256, CTransactionRef> MapRelay;
    MapRelay mapRelay GUARDED_BY(cs_main);
    /** Expiration-time ordered list of (expire time, relay map entry) pairs. */
    std::deque<std::pair<std::chrono::microseconds, MapRelay::iterator>> g_relay_expiration GUARDED_BY(cs_main);

    /**
     * When a peer sends us a valid block, instruct it to announce blocks to us
     * using CMPCTBLOCK if possible by adding its nodeid to the end of
     * lNodesAnnouncingHeaderAndIDs, and keeping that list under a certain size by
     * removing the first element if necessary.
     */
    void MaybeSetPeerAsAnnouncingHeaderAndIDs(NodeId nodeid) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Stack of nodes which we have set to announce using compact blocks */
    std::list<NodeId> lNodesAnnouncingHeaderAndIDs GUARDED_BY(cs_main);

    /** Number of peers from which we're downloading blocks. */
    int m_peers_downloading_from GUARDED_BY(cs_main) = 0;

    /** Storage for orphan information */
    TxOrphanage m_orphanage;

    void AddToCompactExtraTransactions(const CTransactionRef& tx) EXCLUSIVE_LOCKS_REQUIRED(g_cs_orphans);

    /** Orphan/conflicted/etc transactions that are kept for compact block reconstruction.
     *  The last -blockreconstructionextratxn/DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN of
     *  these are kept in a ring buffer */
    std::vector<std::pair<uint256, CTransactionRef>> vExtraTxnForCompact GUARDED_BY(g_cs_orphans);
    /** Offset into vExtraTxnForCompact to insert the next tx */
    size_t vExtraTxnForCompactIt GUARDED_BY(g_cs_orphans) = 0;

    /** Check whether the last unknown block a peer advertised is not yet known. */
    void ProcessBlockAvailability(NodeId nodeid) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Update tracking information about which blocks a peer is assumed to have. */
    void UpdateBlockAvailability(NodeId nodeid, const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool CanDirectFetch() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * To prevent fingerprinting attacks, only send blocks/headers outside of
     * the active chain if they are no more than a month older (both in time,
     * and in best equivalent proof of work) than the best header chain we know
     * about and we fully-validated them at some point.
     */
    bool BlockRequestAllowed(const CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool AlreadyHaveBlock(const uint256& block_hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ProcessGetBlockData(CNode& pfrom, Peer& peer, const CInv& inv);

    /**
     * Validation logic for compact filters request handling.
     *
     * May disconnect from the peer in the case of a bad request.
     *
     * @param[in]   peer            The peer that we received the request from
     * @param[in]   filter_type     The filter type the request is for. Must be basic filters.
     * @param[in]   start_height    The start height for the request
     * @param[in]   stop_hash       The stop_hash for the request
     * @param[in]   max_height_diff The maximum number of items permitted to request, as specified in BIP 157
     * @param[out]  stop_index      The CBlockIndex for the stop_hash block, if the request can be serviced.
     * @param[out]  filter_index    The filter index, if the request can be serviced.
     * @return                      True if the request can be serviced.
     */
    bool PrepareBlockFilterRequest(CNode& peer,
                                   BlockFilterType filter_type, uint32_t start_height,
                                   const uint256& stop_hash, uint32_t max_height_diff,
                                   const CBlockIndex*& stop_index,
                                   BlockFilterIndex*& filter_index);

    /**
     * Handle a cfilters request.
     *
     * May disconnect from the peer in the case of a bad request.
     *
     * @param[in]   peer            The peer that we received the request from
     * @param[in]   vRecv           The raw message received
     */
    void ProcessGetCFilters(CNode& peer, CDataStream& vRecv);

    /**
     * Handle a cfheaders request.
     *
     * May disconnect from the peer in the case of a bad request.
     *
     * @param[in]   peer            The peer that we received the request from
     * @param[in]   vRecv           The raw message received
     */
    void ProcessGetCFHeaders(CNode& peer, CDataStream& vRecv);

    /**
     * Handle a getcfcheckpt request.
     *
     * May disconnect from the peer in the case of a bad request.
     *
     * @param[in]   peer            The peer that we received the request from
     * @param[in]   vRecv           The raw message received
     */
    void ProcessGetCFCheckPt(CNode& peer, CDataStream& vRecv);

    /** Checks if address relay is permitted with peer. If needed, initializes
     * the m_addr_known bloom filter and sets m_addr_relay_enabled to true.
     *
     *  @return   True if address relay is enabled with peer
     *            False if address relay is disallowed
     */
    bool SetupAddressRelay(const CNode& node, Peer& peer);
};
} // namespace

namespace {
    /** Number of preferable block download peers. */
    int nPreferredDownload GUARDED_BY(cs_main) = 0;
} // namespace

namespace {
/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The best known block we know this peer has announced.
    const CBlockIndex* pindexBestKnownBlock{nullptr};
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock{};
    //! The last full block we both have.
    const CBlockIndex* pindexLastCommonBlock{nullptr};
    //! The best header we have sent our peer.
    const CBlockIndex* pindexBestHeaderSent{nullptr};
    //! Length of current-streak of unconnecting headers announcements
    int nUnconnectingHeaders{0};
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted{false};
    //! When to potentially disconnect peer for stalling headers download
    std::chrono::microseconds m_headers_sync_timeout{0us};
    //! Since when we're stalling block download progress (in microseconds), or 0.
    std::chrono::microseconds m_stalling_since{0us};
    std::list<QueuedBlock> vBlocksInFlight;
    //! When the first entry in vBlocksInFlight started downloading. Don't care when vBlocksInFlight is empty.
    std::chrono::microseconds m_downloading_since{0us};
    int nBlocksInFlight{0};
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload{false};
    //! Whether this peer wants invs or headers (when possible) for block announcements.
    bool fPreferHeaders{false};
    //! Whether this peer wants invs or cmpctblocks (when possible) for block announcements.
    bool fPreferHeaderAndIDs{false};
    /**
      * Whether this peer will send us cmpctblocks if we request them.
      * This is not used to gate request logic, as we really only care about fSupportsDesiredCmpctVersion,
      * but is used as a flag to "lock in" the version of compact blocks (fWantsCmpctWitness) we send.
      */
    bool fProvidesHeaderAndIDs{false};
    //! Whether this peer can give us witnesses
    bool fHaveWitness{false};
    //! Whether this peer wants witnesses in cmpctblocks/blocktxns
    bool fWantsCmpctWitness{false};
    /**
     * If we've announced NODE_WITNESS to this peer: whether the peer sends witnesses in cmpctblocks/blocktxns,
     * otherwise: whether this peer sends non-witnesses in cmpctblocks/blocktxns.
     */
    bool fSupportsDesiredCmpctVersion{false};

    /** State used to enforce CHAIN_SYNC_TIMEOUT and EXTRA_PEER_CHECK_INTERVAL logic.
      *
      * Both are only in effect for outbound, non-manual, non-protected connections.
      * Any peer protected (m_protect = true) is not chosen for eviction. A peer is
      * marked as protected if all of these are true:
      *   - its connection type is IsBlockOnlyConn() == false
      *   - it gave us a valid connecting header
      *   - we haven't reached MAX_OUTBOUND_PEERS_TO_PROTECT_FROM_DISCONNECT yet
      *   - its chain tip has at least as much work as ours
      *
      * CHAIN_SYNC_TIMEOUT: if a peer's best known block has less work than our tip,
      * set a timeout CHAIN_SYNC_TIMEOUT in the future:
      *   - If at timeout their best known block now has more work than our tip
      *     when the timeout was set, then either reset the timeout or clear it
      *     (after comparing against our current tip's work)
      *   - If at timeout their best known block still has less work than our
      *     tip did when the timeout was set, then send a getheaders message,
      *     and set a shorter timeout, HEADERS_RESPONSE_TIME seconds in future.
      *     If their best known block is still behind when that new timeout is
      *     reached, disconnect.
      *
      * EXTRA_PEER_CHECK_INTERVAL: after each interval, if we have too many outbound peers,
      * drop the outbound one that least recently announced us a new block.
      */
    struct ChainSyncTimeoutState {
        //! A timeout used for checking whether our peer has sufficiently synced
        std::chrono::seconds m_timeout{0s};
        //! A header with the work we require on our peer's chain
        const CBlockIndex* m_work_header{nullptr};
        //! After timeout is reached, set to true after sending getheaders
        bool m_sent_getheaders{false};
        //! Whether this peer is protected from disconnection due to a bad/slow chain
        bool m_protect{false};
    };

    ChainSyncTimeoutState m_chain_sync;

    //! Time of last new block announcement
    int64_t m_last_block_announcement{0};

    //! Whether this peer is an inbound connection
    const bool m_is_inbound;

    //! A rolling bloom filter of all announced tx CInvs to this peer.
    CRollingBloomFilter m_recently_announced_invs = CRollingBloomFilter{INVENTORY_MAX_RECENT_RELAY, 0.000001};

    //! Whether this peer relays txs via wtxid
    bool m_wtxid_relay{false};

    CNodeState(bool is_inbound) : m_is_inbound(is_inbound) {}
};

/** Map maintaining per-node state. */
static std::map<NodeId, CNodeState> mapNodeState GUARDED_BY(cs_main);

static CNodeState *State(NodeId pnode) EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return nullptr;
    return &it->second;
}

/**
 * Whether the peer supports the address. For example, a peer that does not
 * implement BIP155 cannot receive Tor v3 addresses because it requires
 * ADDRv2 (BIP155) encoding.
 */
static bool IsAddrCompatible(const Peer& peer, const CAddress& addr)
{
    return peer.m_wants_addrv2 || addr.IsAddrV1Compatible();
}

static void AddAddressKnown(Peer& peer, const CAddress& addr)
{
    assert(peer.m_addr_known);
    peer.m_addr_known->insert(addr.GetKey());
}

static void PushAddress(Peer& peer, const CAddress& addr, FastRandomContext& insecure_rand)
{
    // Known checking here is only to save space from duplicates.
    // Before sending, we'll filter it again for known addresses that were
    // added after addresses were pushed.
    assert(peer.m_addr_known);
    if (addr.IsValid() && !peer.m_addr_known->contains(addr.GetKey()) && IsAddrCompatible(peer, addr)) {
        if (peer.m_addrs_to_send.size() >= MAX_ADDR_TO_SEND) {
            peer.m_addrs_to_send[insecure_rand.randrange(peer.m_addrs_to_send.size())] = addr;
        } else {
            peer.m_addrs_to_send.push_back(addr);
        }
    }
}

static void UpdatePreferredDownload(const CNode& node, CNodeState* state) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node.IsInboundConn() || node.HasPermission(NetPermissionFlags::NoBan)) && !node.IsAddrFetchConn() && !node.fClient;

    nPreferredDownload += state->fPreferredDownload;
}

bool PeerManagerImpl::IsBlockRequested(const uint256& hash)
{
    return mapBlocksInFlight.find(hash) != mapBlocksInFlight.end();
}

void PeerManagerImpl::RemoveBlockRequest(const uint256& hash)
{
    auto it = mapBlocksInFlight.find(hash);
    if (it == mapBlocksInFlight.end()) {
        // Block was not requested
        return;
    }

    auto [node_id, list_it] = it->second;
    CNodeState *state = State(node_id);
    assert(state != nullptr);

    if (state->vBlocksInFlight.begin() == list_it) {
        // First block on the queue was received, update the start download time for the next one
        state->m_downloading_since = std::max(state->m_downloading_since, GetTime<std::chrono::microseconds>());
    }
    state->vBlocksInFlight.erase(list_it);

    state->nBlocksInFlight--;
    if (state->nBlocksInFlight == 0) {
        // Last validated block on the queue was received.
        m_peers_downloading_from--;
    }
    state->m_stalling_since = 0us;
    mapBlocksInFlight.erase(it);
}

bool PeerManagerImpl::BlockRequested(NodeId nodeid, const CBlockIndex& block, std::list<QueuedBlock>::iterator** pit)
{
    const uint256& hash{block.GetBlockHash()};

    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    // Short-circuit most stuff in case it is from the same node
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end() && itInFlight->second.first == nodeid) {
        if (pit) {
            *pit = &itInFlight->second.second;
        }
        return false;
    }

    // Make sure it's not listed somewhere already.
    RemoveBlockRequest(hash);

    std::list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(),
            {&block, std::unique_ptr<PartiallyDownloadedBlock>(pit ? new PartiallyDownloadedBlock(&m_mempool) : nullptr)});
    state->nBlocksInFlight++;
    if (state->nBlocksInFlight == 1) {
        // We're starting a block download (batch) from this peer.
        state->m_downloading_since = GetTime<std::chrono::microseconds>();
        m_peers_downloading_from++;
    }
    itInFlight = mapBlocksInFlight.insert(std::make_pair(hash, std::make_pair(nodeid, it))).first;
    if (pit) {
        *pit = &itInFlight->second.second;
    }
    return true;
}

void PeerManagerImpl::MaybeSetPeerAsAnnouncingHeaderAndIDs(NodeId nodeid)
{
    AssertLockHeld(cs_main);

    // Never request high-bandwidth mode from peers if we're blocks-only. Our
    // mempool will not contain the transactions necessary to reconstruct the
    // compact block.
    if (m_ignore_incoming_txs) return;

    CNodeState* nodestate = State(nodeid);
    if (!nodestate || !nodestate->fSupportsDesiredCmpctVersion) {
        // Never ask from peers who can't provide witnesses.
        return;
    }
    if (nodestate->fProvidesHeaderAndIDs) {
        int num_outbound_hb_peers = 0;
        for (std::list<NodeId>::iterator it = lNodesAnnouncingHeaderAndIDs.begin(); it != lNodesAnnouncingHeaderAndIDs.end(); it++) {
            if (*it == nodeid) {
                lNodesAnnouncingHeaderAndIDs.erase(it);
                lNodesAnnouncingHeaderAndIDs.push_back(nodeid);
                return;
            }
            CNodeState *state = State(*it);
            if (state != nullptr && !state->m_is_inbound) ++num_outbound_hb_peers;
        }
        if (nodestate->m_is_inbound) {
            // If we're adding an inbound HB peer, make sure we're not removing
            // our last outbound HB peer in the process.
            if (lNodesAnnouncingHeaderAndIDs.size() >= 3 && num_outbound_hb_peers == 1) {
                CNodeState *remove_node = State(lNodesAnnouncingHeaderAndIDs.front());
                if (remove_node != nullptr && !remove_node->m_is_inbound) {
                    // Put the HB outbound peer in the second slot, so that it
                    // doesn't get removed.
                    std::swap(lNodesAnnouncingHeaderAndIDs.front(), *std::next(lNodesAnnouncingHeaderAndIDs.begin()));
                }
            }
        }
        m_connman.ForNode(nodeid, [this](CNode* pfrom) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
            AssertLockHeld(::cs_main);
            uint64_t nCMPCTBLOCKVersion = 2;
            if (lNodesAnnouncingHeaderAndIDs.size() >= 3) {
                // As per BIP152, we only get 3 of our peers to announce
                // blocks using compact encodings.
                m_connman.ForNode(lNodesAnnouncingHeaderAndIDs.front(), [this, nCMPCTBLOCKVersion](CNode* pnodeStop){
                    m_connman.PushMessage(pnodeStop, CNetMsgMaker(pnodeStop->GetCommonVersion()).Make(NetMsgType::SENDCMPCT, /*fAnnounceUsingCMPCTBLOCK=*/false, nCMPCTBLOCKVersion));
                    // save BIP152 bandwidth state: we select peer to be low-bandwidth
                    pnodeStop->m_bip152_highbandwidth_to = false;
                    return true;
                });
                lNodesAnnouncingHeaderAndIDs.pop_front();
            }
            m_connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetCommonVersion()).Make(NetMsgType::SENDCMPCT, /*fAnnounceUsingCMPCTBLOCK=*/true, nCMPCTBLOCKVersion));
            // save BIP152 bandwidth state: we select peer to be high-bandwidth
            pfrom->m_bip152_highbandwidth_to = true;
            lNodesAnnouncingHeaderAndIDs.push_back(pfrom->GetId());
            return true;
        });
    }
}

bool PeerManagerImpl::TipMayBeStale()
{
    AssertLockHeld(cs_main);
    const Consensus::Params& consensusParams = m_chainparams.GetConsensus();
    if (m_last_tip_update.load() == 0s) {
        m_last_tip_update = GetTime<std::chrono::seconds>();
    }
    return m_last_tip_update.load() < GetTime<std::chrono::seconds>() - std::chrono::seconds{consensusParams.nPowTargetSpacing * 3} && mapBlocksInFlight.empty();
}

bool PeerManagerImpl::CanDirectFetch()
{
    return m_chainman.ActiveChain().Tip()->GetBlockTime() > GetAdjustedTime() - m_chainparams.GetConsensus().nPowTargetSpacing * 20;
}

static bool PeerHasHeader(CNodeState *state, const CBlockIndex *pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    if (state->pindexBestKnownBlock && pindex == state->pindexBestKnownBlock->GetAncestor(pindex->nHeight))
        return true;
    if (state->pindexBestHeaderSent && pindex == state->pindexBestHeaderSent->GetAncestor(pindex->nHeight))
        return true;
    return false;
}

void PeerManagerImpl::ProcessBlockAvailability(NodeId nodeid) {
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    if (!state->hashLastUnknownBlock.IsNull()) {
        const CBlockIndex* pindex = m_chainman.m_blockman.LookupBlockIndex(state->hashLastUnknownBlock);
        if (pindex && pindex->nChainWork > 0) {
            if (state->pindexBestKnownBlock == nullptr || pindex->nChainWork >= state->pindexBestKnownBlock->nChainWork) {
                state->pindexBestKnownBlock = pindex;
            }
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

void PeerManagerImpl::UpdateBlockAvailability(NodeId nodeid, const uint256 &hash) {
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    ProcessBlockAvailability(nodeid);

    const CBlockIndex* pindex = m_chainman.m_blockman.LookupBlockIndex(hash);
    if (pindex && pindex->nChainWork > 0) {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == nullptr || pindex->nChainWork >= state->pindexBestKnownBlock->nChainWork) {
            state->pindexBestKnownBlock = pindex;
        }
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

void PeerManagerImpl::FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<const CBlockIndex*>& vBlocks, NodeId& nodeStaller)
{
    if (count == 0)
        return;

    vBlocks.reserve(vBlocks.size() + count);
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(nodeid);

    if (state->pindexBestKnownBlock == nullptr || state->pindexBestKnownBlock->nChainWork < m_chainman.ActiveChain().Tip()->nChainWork || state->pindexBestKnownBlock->nChainWork < nMinimumChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == nullptr) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = m_chainman.ActiveChain()[std::min(state->pindexBestKnownBlock->nHeight, m_chainman.ActiveChain().Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of its current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    const Consensus::Params& consensusParams = m_chainparams.GetConsensus();
    std::vector<const CBlockIndex*> vToFetch;
    const CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    while (pindexWalk->nHeight < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight, std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the meantime, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
        // already part of our chain (and therefore don't need it even if pruned).
        for (const CBlockIndex* pindex : vToFetch) {
            if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (!State(nodeid)->fHaveWitness && DeploymentActiveAt(*pindex, consensusParams, Consensus::DEPLOYMENT_SEGWIT)) {
                // We wouldn't download this block or its descendants from this peer.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA || m_chainman.ActiveChain().Contains(pindex)) {
                if (pindex->HaveTxsDownloaded())
                    state->pindexLastCommonBlock = pindex;
            } else if (!IsBlockRequested(pindex->GetBlockHash())) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd) {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != nodeid) {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

} // namespace

void PeerManagerImpl::PushNodeVersion(CNode& pnode)
{
    // Note that pnode->GetLocalServices() is a reflection of the local
    // services we were offering when the CNode object was created for this
    // peer.
    uint64_t my_services{pnode.GetLocalServices()};
    const int64_t nTime{count_seconds(GetTime<std::chrono::seconds>())};
    uint64_t nonce = pnode.GetLocalNonce();
    const int nNodeStartingHeight{m_best_height};
    NodeId nodeid = pnode.GetId();
    CAddress addr = pnode.addr;

    CService addr_you = addr.IsRoutable() && !IsProxy(addr) && addr.IsAddrV1Compatible() ? addr : CService();
    uint64_t your_services{addr.nServices};

    const bool tx_relay = !m_ignore_incoming_txs && pnode.m_tx_relay != nullptr && !pnode.IsFeelerConn();
    m_connman.PushMessage(&pnode, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERSION, PROTOCOL_VERSION, my_services, nTime,
            your_services, addr_you, // Together the pre-version-31402 serialization of CAddress "addrYou" (without nTime)
            my_services, CService(), // Together the pre-version-31402 serialization of CAddress "addrMe" (without nTime)
            nonce, strSubVersion, nNodeStartingHeight, tx_relay));

    if (fLogIPs) {
        LogPrint(BCLog::NET, "send version message: version %d, blocks=%d, them=%s, txrelay=%d, peer=%d\n", PROTOCOL_VERSION, nNodeStartingHeight, addr_you.ToString(), tx_relay, nodeid);
    } else {
        LogPrint(BCLog::NET, "send version message: version %d, blocks=%d, txrelay=%d, peer=%d\n", PROTOCOL_VERSION, nNodeStartingHeight, tx_relay, nodeid);
    }
}

void PeerManagerImpl::AddTxAnnouncement(const CNode& node, const GenTxid& gtxid, std::chrono::microseconds current_time)
{
    AssertLockHeld(::cs_main); // For m_txrequest
    NodeId nodeid = node.GetId();
    if (!node.HasPermission(NetPermissionFlags::Relay) && m_txrequest.Count(nodeid) >= MAX_PEER_TX_ANNOUNCEMENTS) {
        // Too many queued announcements from this peer
        return;
    }
    const CNodeState* state = State(nodeid);

    // Decide the TxRequestTracker parameters for this announcement:
    // - "preferred": if fPreferredDownload is set (= outbound, or NetPermissionFlags::NoBan permission)
    // - "reqtime": current time plus delays for:
    //   - NONPREF_PEER_TX_DELAY for announcements from non-preferred connections
    //   - TXID_RELAY_DELAY for txid announcements while wtxid peers are available
    //   - OVERLOADED_PEER_TX_DELAY for announcements from peers which have at least
    //     MAX_PEER_TX_REQUEST_IN_FLIGHT requests in flight (and don't have NetPermissionFlags::Relay).
    auto delay{0us};
    const bool preferred = state->fPreferredDownload;
    if (!preferred) delay += NONPREF_PEER_TX_DELAY;
    if (!gtxid.IsWtxid() && m_wtxid_relay_peers > 0) delay += TXID_RELAY_DELAY;
    const bool overloaded = !node.HasPermission(NetPermissionFlags::Relay) &&
        m_txrequest.CountInFlight(nodeid) >= MAX_PEER_TX_REQUEST_IN_FLIGHT;
    if (overloaded) delay += OVERLOADED_PEER_TX_DELAY;
    m_txrequest.ReceivedInv(nodeid, gtxid, preferred, current_time + delay);
}

// This function is used for testing the stale tip eviction logic, see
// denialofservice_tests.cpp
void UpdateLastBlockAnnounceTime(NodeId node, int64_t time_in_seconds)
{
    LOCK(cs_main);
    CNodeState *state = State(node);
    if (state) state->m_last_block_announcement = time_in_seconds;
}

void PeerManagerImpl::InitializeNode(CNode *pnode)
{
    NodeId nodeid = pnode->GetId();
    {
        LOCK(cs_main);
        mapNodeState.emplace_hint(mapNodeState.end(), std::piecewise_construct, std::forward_as_tuple(nodeid), std::forward_as_tuple(pnode->IsInboundConn()));
        assert(m_txrequest.Count(nodeid) == 0);
    }
    {
        PeerRef peer = std::make_shared<Peer>(nodeid);
        LOCK(m_peer_mutex);
        m_peer_map.emplace_hint(m_peer_map.end(), nodeid, std::move(peer));
    }
    if (!pnode->IsInboundConn()) {
        PushNodeVersion(*pnode);
    }
}

void PeerManagerImpl::ReattemptInitialBroadcast(CScheduler& scheduler)
{
    std::set<uint256> unbroadcast_txids = m_mempool.GetUnbroadcastTxs();

    for (const auto& txid : unbroadcast_txids) {
        CTransactionRef tx = m_mempool.get(txid);

        if (tx != nullptr) {
            LOCK(cs_main);
            _RelayTransaction(txid, tx->GetWitnessHash());
        } else {
            m_mempool.RemoveUnbroadcastTx(txid, true);
        }
    }

    // Schedule next run for 10-15 minutes in the future.
    // We add randomness on every cycle to avoid the possibility of P2P fingerprinting.
    const std::chrono::milliseconds delta = 10min + GetRandMillis(5min);
    scheduler.scheduleFromNow([&] { ReattemptInitialBroadcast(scheduler); }, delta);
}

void PeerManagerImpl::FinalizeNode(const CNode& node)
{
    NodeId nodeid = node.GetId();
    int misbehavior{0};
    {
    LOCK(cs_main);
    {
        // We remove the PeerRef from g_peer_map here, but we don't always
        // destruct the Peer. Sometimes another thread is still holding a
        // PeerRef, so the refcount is >= 1. Be careful not to do any
        // processing here that assumes Peer won't be changed before it's
        // destructed.
        PeerRef peer = RemovePeer(nodeid);
        assert(peer != nullptr);
        misbehavior = WITH_LOCK(peer->m_misbehavior_mutex, return peer->m_misbehavior_score);
    }
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    if (state->fSyncStarted)
        nSyncStarted--;

    for (const QueuedBlock& entry : state->vBlocksInFlight) {
        mapBlocksInFlight.erase(entry.pindex->GetBlockHash());
    }
    WITH_LOCK(g_cs_orphans, m_orphanage.EraseForPeer(nodeid));
    m_txrequest.DisconnectedPeer(nodeid);
    nPreferredDownload -= state->fPreferredDownload;
    m_peers_downloading_from -= (state->nBlocksInFlight != 0);
    assert(m_peers_downloading_from >= 0);
    m_outbound_peers_with_protect_from_disconnect -= state->m_chain_sync.m_protect;
    assert(m_outbound_peers_with_protect_from_disconnect >= 0);
    m_wtxid_relay_peers -= state->m_wtxid_relay;
    assert(m_wtxid_relay_peers >= 0);

    mapNodeState.erase(nodeid);

    if (mapNodeState.empty()) {
        // Do a consistency check after the last peer is removed.
        assert(mapBlocksInFlight.empty());
        assert(nPreferredDownload == 0);
        assert(m_peers_downloading_from == 0);
        assert(m_outbound_peers_with_protect_from_disconnect == 0);
        assert(m_wtxid_relay_peers == 0);
        assert(m_txrequest.Size() == 0);
        assert(m_orphanage.Size() == 0);
    }
    } // cs_main
    if (node.fSuccessfullyConnected && misbehavior == 0 &&
        !node.IsBlockOnlyConn() && !node.IsInboundConn()) {
        // Only change visible addrman state for full outbound peers.  We don't
        // call Connected() for feeler connections since they don't have
        // fSuccessfullyConnected set.
        m_addrman.Connected(node.addr);
    }
    LogPrint(BCLog::NET, "Cleared nodestate for peer=%d\n", nodeid);
}

PeerRef PeerManagerImpl::GetPeerRef(NodeId id) const
{
    LOCK(m_peer_mutex);
    auto it = m_peer_map.find(id);
    return it != m_peer_map.end() ? it->second : nullptr;
}

PeerRef PeerManagerImpl::RemovePeer(NodeId id)
{
    PeerRef ret;
    LOCK(m_peer_mutex);
    auto it = m_peer_map.find(id);
    if (it != m_peer_map.end()) {
        ret = std::move(it->second);
        m_peer_map.erase(it);
    }
    return ret;
}

bool PeerManagerImpl::GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats) const
{
    {
        LOCK(cs_main);
        CNodeState* state = State(nodeid);
        if (state == nullptr)
            return false;
        stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
        stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
        for (const QueuedBlock& queue : state->vBlocksInFlight) {
            if (queue.pindex)
                stats.vHeightInFlight.push_back(queue.pindex->nHeight);
        }
    }

    PeerRef peer = GetPeerRef(nodeid);
    if (peer == nullptr) return false;
    stats.m_starting_height = peer->m_starting_height;
    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer.
    // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
    // since pingtime does not update until the ping is complete, which might take a while.
    // So, if a ping is taking an unusually long time in flight,
    // the caller can immediately detect that this is happening.
    auto ping_wait{0us};
    if ((0 != peer->m_ping_nonce_sent) && (0 != peer->m_ping_start.load().count())) {
        ping_wait = GetTime<std::chrono::microseconds>() - peer->m_ping_start.load();
    }

    stats.m_ping_wait = ping_wait;
    stats.m_addr_processed = peer->m_addr_processed.load();
    stats.m_addr_rate_limited = peer->m_addr_rate_limited.load();
    stats.m_addr_relay_enabled = peer->m_addr_relay_enabled.load();

    return true;
}

void PeerManagerImpl::AddToCompactExtraTransactions(const CTransactionRef& tx)
{
    size_t max_extra_txn = gArgs.GetIntArg("-blockreconstructionextratxn", DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN);
    if (max_extra_txn <= 0)
        return;
    if (!vExtraTxnForCompact.size())
        vExtraTxnForCompact.resize(max_extra_txn);
    vExtraTxnForCompact[vExtraTxnForCompactIt] = std::make_pair(tx->GetWitnessHash(), tx);
    vExtraTxnForCompactIt = (vExtraTxnForCompactIt + 1) % max_extra_txn;
}

void PeerManagerImpl::Misbehaving(const NodeId pnode, const int howmuch, const std::string& message)
{
    assert(howmuch > 0);

    PeerRef peer = GetPeerRef(pnode);
    if (peer == nullptr) return;

    LOCK(peer->m_misbehavior_mutex);
    const int score_before{peer->m_misbehavior_score};
    peer->m_misbehavior_score += howmuch;
    const int score_now{peer->m_misbehavior_score};

    const std::string message_prefixed = message.empty() ? "" : (": " + message);
    std::string warning;

    if (score_now >= DISCOURAGEMENT_THRESHOLD && score_before < DISCOURAGEMENT_THRESHOLD) {
        warning = " DISCOURAGE THRESHOLD EXCEEDED";
        peer->m_should_discourage = true;
    }

    LogPrint(BCLog::NET, "Misbehaving: peer=%d (%d -> %d)%s%s\n",
             pnode, score_before, score_now, warning, message_prefixed);
}

bool PeerManagerImpl::MaybePunishNodeForBlock(NodeId nodeid, const BlockValidationState& state,
                                              bool via_compact_block, const std::string& message)
{
    switch (state.GetResult()) {
    case BlockValidationResult::BLOCK_RESULT_UNSET:
        break;
    // The node is providing invalid data:
    case BlockValidationResult::BLOCK_CONSENSUS:
    case BlockValidationResult::BLOCK_MUTATED:
        if (!via_compact_block) {
            Misbehaving(nodeid, 100, message);
            return true;
        }
        break;
    case BlockValidationResult::BLOCK_CACHED_INVALID:
        {
            LOCK(cs_main);
            CNodeState *node_state = State(nodeid);
            if (node_state == nullptr) {
                break;
            }

            // Discourage outbound (but not inbound) peers if on an invalid chain.
            // Exempt HB compact block peers. Manual connections are always protected from discouragement.
            if (!via_compact_block && !node_state->m_is_inbound) {
                Misbehaving(nodeid, 100, message);
                return true;
            }
            break;
        }
    case BlockValidationResult::BLOCK_INVALID_HEADER:
    case BlockValidationResult::BLOCK_CHECKPOINT:
    case BlockValidationResult::BLOCK_INVALID_PREV:
        Misbehaving(nodeid, 100, message);
        return true;
    // Conflicting (but not necessarily invalid) data or different policy:
    case BlockValidationResult::BLOCK_MISSING_PREV:
        // TODO: Handle this much more gracefully (10 DoS points is super arbitrary)
        Misbehaving(nodeid, 10, message);
        return true;
    case BlockValidationResult::BLOCK_RECENT_CONSENSUS_CHANGE:
    case BlockValidationResult::BLOCK_TIME_FUTURE:
        break;
    }
    if (message != "") {
        LogPrint(BCLog::NET, "peer=%d: %s\n", nodeid, message);
    }
    return false;
}

bool PeerManagerImpl::MaybePunishNodeForTx(NodeId nodeid, const TxValidationState& state, const std::string& message)
{
    switch (state.GetResult()) {
    case TxValidationResult::TX_RESULT_UNSET:
        break;
    // The node is providing invalid data:
    case TxValidationResult::TX_CONSENSUS:
        Misbehaving(nodeid, 100, message);
        return true;
    // Conflicting (but not necessarily invalid) data or different policy:
    case TxValidationResult::TX_RECENT_CONSENSUS_CHANGE:
    case TxValidationResult::TX_INPUTS_NOT_STANDARD:
    case TxValidationResult::TX_NOT_STANDARD:
    case TxValidationResult::TX_MISSING_INPUTS:
    case TxValidationResult::TX_PREMATURE_SPEND:
    case TxValidationResult::TX_WITNESS_MUTATED:
    case TxValidationResult::TX_WITNESS_STRIPPED:
    case TxValidationResult::TX_CONFLICT:
    case TxValidationResult::TX_MEMPOOL_POLICY:
    case TxValidationResult::TX_NO_MEMPOOL:
        break;
    }
    if (message != "") {
        LogPrint(BCLog::NET, "peer=%d: %s\n", nodeid, message);
    }
    return false;
}

bool PeerManagerImpl::BlockRequestAllowed(const CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);
    if (m_chainman.ActiveChain().Contains(pindex)) return true;
    return pindex->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != nullptr) &&
           (pindexBestHeader->GetBlockTime() - pindex->GetBlockTime() < STALE_RELAY_AGE_LIMIT) &&
           (GetBlockProofEquivalentTime(*pindexBestHeader, *pindex, *pindexBestHeader, m_chainparams.GetConsensus()) < STALE_RELAY_AGE_LIMIT);
}

bool PeerManagerImpl::FetchBlock(NodeId id, const uint256& hash, const CBlockIndex& index)
{
    if (fImporting || fReindex) return false;

    LOCK(cs_main);
    // Ensure this peer exists and hasn't been disconnected
    CNodeState* state = State(id);
    if (state == nullptr) return false;
    // Ignore pre-segwit peers
    if (!state->fHaveWitness) return false;

    // Mark block as in-flight unless it already is
    if (!BlockRequested(id, index)) return false;

    // Construct message to request the block
    std::vector<CInv> invs{CInv(MSG_BLOCK | MSG_WITNESS_FLAG, hash)};

    // Send block request message to the peer
    bool success = m_connman.ForNode(id, [this, &invs](CNode* node) {
        const CNetMsgMaker msgMaker(node->GetCommonVersion());
        this->m_connman.PushMessage(node, msgMaker.Make(NetMsgType::GETDATA, invs));
        return true;
    });

    if (success) {
        LogPrint(BCLog::NET, "Requesting block %s from peer=%d\n",
                 hash.ToString(), id);
    } else {
        RemoveBlockRequest(hash);
        LogPrint(BCLog::NET, "Failed to request block %s from peer=%d\n",
                 hash.ToString(), id);
    }
    return success;
}

std::unique_ptr<PeerManager> PeerManager::make(const CChainParams& chainparams, CConnman& connman, AddrMan& addrman,
                                               BanMan* banman, ChainstateManager& chainman,
                                               CTxMemPool& pool, bool ignore_incoming_txs)
{
    return std::make_unique<PeerManagerImpl>(chainparams, connman, addrman, banman, chainman, pool, ignore_incoming_txs);
}

PeerManagerImpl::PeerManagerImpl(const CChainParams& chainparams, CConnman& connman, AddrMan& addrman,
                                 BanMan* banman, ChainstateManager& chainman,
                                 CTxMemPool& pool, bool ignore_incoming_txs)
    : m_chainparams(chainparams),
      m_connman(connman),
      m_addrman(addrman),
      m_banman(banman),
      m_chainman(chainman),
      m_mempool(pool),
      m_ignore_incoming_txs(ignore_incoming_txs)
{
}

void PeerManagerImpl::StartScheduledTasks(CScheduler& scheduler)
{
    // Stale tip checking and peer eviction are on two different timers, but we
    // don't want them to get out of sync due to drift in the scheduler, so we
    // combine them in one function and schedule at the quicker (peer-eviction)
    // timer.
    static_assert(EXTRA_PEER_CHECK_INTERVAL < STALE_CHECK_INTERVAL, "peer eviction timer should be less than stale tip check timer");
    scheduler.scheduleEvery([this] { this->CheckForStaleTipAndEvictPeers(); }, std::chrono::seconds{EXTRA_PEER_CHECK_INTERVAL});

    // schedule next run for 10-15 minutes in the future
    const std::chrono::milliseconds delta = 10min + GetRandMillis(5min);
    scheduler.scheduleFromNow([&] { ReattemptInitialBroadcast(scheduler); }, delta);
}

/**
 * Evict orphan txn pool entries based on a newly connected
 * block, remember the recently confirmed transactions, and delete tracked
 * announcements for them. Also save the time of the last tip update.
 */
void PeerManagerImpl::BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindex)
{
    m_orphanage.EraseForBlock(*pblock);
    m_last_tip_update = GetTime<std::chrono::seconds>();

    {
        LOCK(m_recent_confirmed_transactions_mutex);
        for (const auto& ptx : pblock->vtx) {
            m_recent_confirmed_transactions.insert(ptx->GetHash());
            if (ptx->GetHash() != ptx->GetWitnessHash()) {
                m_recent_confirmed_transactions.insert(ptx->GetWitnessHash());
            }
        }
    }
    {
        LOCK(cs_main);
        for (const auto& ptx : pblock->vtx) {
            m_txrequest.ForgetTxHash(ptx->GetHash());
            m_txrequest.ForgetTxHash(ptx->GetWitnessHash());
        }
    }
}

void PeerManagerImpl::BlockDisconnected(const std::shared_ptr<const CBlock> &block, const CBlockIndex* pindex)
{
    // To avoid relay problems with transactions that were previously
    // confirmed, clear our filter of recently confirmed transactions whenever
    // there's a reorg.
    // This means that in a 1-block reorg (where 1 block is disconnected and
    // then another block reconnected), our filter will drop to having only one
    // block's worth of transactions in it, but that should be fine, since
    // presumably the most common case of relaying a confirmed transaction
    // should be just after a new block containing it is found.
    LOCK(m_recent_confirmed_transactions_mutex);
    m_recent_confirmed_transactions.reset();
}

// All of the following cache a recent block, and are protected by cs_most_recent_block
static RecursiveMutex cs_most_recent_block;
static std::shared_ptr<const CBlock> most_recent_block GUARDED_BY(cs_most_recent_block);
static std::shared_ptr<const CBlockHeaderAndShortTxIDs> most_recent_compact_block GUARDED_BY(cs_most_recent_block);
static uint256 most_recent_block_hash GUARDED_BY(cs_most_recent_block);
static bool fWitnessesPresentInMostRecentCompactBlock GUARDED_BY(cs_most_recent_block);

/**
 * Maintain state about the best-seen block and fast-announce a compact block
 * to compatible peers.
 */
void PeerManagerImpl::NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock>& pblock)
{
    std::shared_ptr<const CBlockHeaderAndShortTxIDs> pcmpctblock = std::make_shared<const CBlockHeaderAndShortTxIDs> (*pblock, true);
    const CNetMsgMaker msgMaker(PROTOCOL_VERSION);

    LOCK(cs_main);

    static int nHighestFastAnnounce = 0;
    if (pindex->nHeight <= nHighestFastAnnounce)
        return;
    nHighestFastAnnounce = pindex->nHeight;

    bool fWitnessEnabled = DeploymentActiveAt(*pindex, m_chainparams.GetConsensus(), Consensus::DEPLOYMENT_SEGWIT);
    uint256 hashBlock(pblock->GetHash());

    {
        LOCK(cs_most_recent_block);
        most_recent_block_hash = hashBlock;
        most_recent_block = pblock;
        most_recent_compact_block = pcmpctblock;
        fWitnessesPresentInMostRecentCompactBlock = fWitnessEnabled;
    }

    m_connman.ForEachNode([this, &pcmpctblock, pindex, &msgMaker, fWitnessEnabled, &hashBlock](CNode* pnode) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        AssertLockHeld(::cs_main);

        // TODO: Avoid the repeated-serialization here
        if (pnode->GetCommonVersion() < INVALID_CB_NO_BAN_VERSION || pnode->fDisconnect)
            return;
        ProcessBlockAvailability(pnode->GetId());
        CNodeState &state = *State(pnode->GetId());
        // If the peer has, or we announced to them the previous block already,
        // but we don't think they have this one, go ahead and announce it
        if (state.fPreferHeaderAndIDs && (!fWitnessEnabled || state.fWantsCmpctWitness) &&
                !PeerHasHeader(&state, pindex) && PeerHasHeader(&state, pindex->pprev)) {

            LogPrint(BCLog::NET, "%s sending header-and-ids %s to peer=%d\n", "PeerManager::NewPoWValidBlock",
                    hashBlock.ToString(), pnode->GetId());
            m_connman.PushMessage(pnode, msgMaker.Make(NetMsgType::CMPCTBLOCK, *pcmpctblock));
            state.pindexBestHeaderSent = pindex;
        }
    });
}

/**
 * Update our best height and announce any block hashes which weren't previously
 * in m_chainman.ActiveChain() to our peers.
 */
void PeerManagerImpl::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload)
{
    SetBestHeight(pindexNew->nHeight);
    SetServiceFlagsIBDCache(!fInitialDownload);

    // Don't relay inventory during initial block download.
    if (fInitialDownload) return;

    // Find the hashes of all blocks that weren't previously in the best chain.
    std::vector<uint256> vHashes;
    const CBlockIndex *pindexToAnnounce = pindexNew;
    while (pindexToAnnounce != pindexFork) {
        vHashes.push_back(pindexToAnnounce->GetBlockHash());
        pindexToAnnounce = pindexToAnnounce->pprev;
        if (vHashes.size() == MAX_BLOCKS_TO_ANNOUNCE) {
            // Limit announcements in case of a huge reorganization.
            // Rely on the peer's synchronization mechanism in that case.
            break;
        }
    }

    {
        LOCK(m_peer_mutex);
        for (auto& it : m_peer_map) {
            Peer& peer = *it.second;
            LOCK(peer.m_block_inv_mutex);
            for (const uint256& hash : reverse_iterate(vHashes)) {
                peer.m_blocks_for_headers_relay.push_back(hash);
            }
        }
    }

    m_connman.WakeMessageHandler();
}

/**
 * Handle invalid block rejection and consequent peer discouragement, maintain which
 * peers announce compact blocks.
 */
void PeerManagerImpl::BlockChecked(const CBlock& block, const BlockValidationState& state)
{
    LOCK(cs_main);

    const uint256 hash(block.GetHash());
    std::map<uint256, std::pair<NodeId, bool>>::iterator it = mapBlockSource.find(hash);

    // If the block failed validation, we know where it came from and we're still connected
    // to that peer, maybe punish.
    if (state.IsInvalid() &&
        it != mapBlockSource.end() &&
        State(it->second.first)) {
            MaybePunishNodeForBlock(/*nodeid=*/ it->second.first, state, /*via_compact_block=*/ !it->second.second);
    }
    // Check that:
    // 1. The block is valid
    // 2. We're not in initial block download
    // 3. This is currently the best block we're aware of. We haven't updated
    //    the tip yet so we have no way to check this directly here. Instead we
    //    just check that there are currently no other blocks in flight.
    else if (state.IsValid() &&
             !m_chainman.ActiveChainstate().IsInitialBlockDownload() &&
             mapBlocksInFlight.count(hash) == mapBlocksInFlight.size()) {
        if (it != mapBlockSource.end()) {
            MaybeSetPeerAsAnnouncingHeaderAndIDs(it->second.first);
        }
    }
    if (it != mapBlockSource.end())
        mapBlockSource.erase(it);
}

//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool PeerManagerImpl::AlreadyHaveTx(const GenTxid& gtxid)
{
    if (m_chainman.ActiveChain().Tip()->GetBlockHash() != hashRecentRejectsChainTip) {
        // If the chain tip has changed previously rejected transactions
        // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
        // or a double-spend. Reset the rejects filter and give those
        // txs a second chance.
        hashRecentRejectsChainTip = m_chainman.ActiveChain().Tip()->GetBlockHash();
        m_recent_rejects.reset();
    }

    const uint256& hash = gtxid.GetHash();

    if (m_orphanage.HaveTx(gtxid)) return true;

    {
        LOCK(m_recent_confirmed_transactions_mutex);
        if (m_recent_confirmed_transactions.contains(hash)) return true;
    }

    return m_recent_rejects.contains(hash) || m_mempool.exists(gtxid);
}

bool PeerManagerImpl::AlreadyHaveBlock(const uint256& block_hash)
{
    return m_chainman.m_blockman.LookupBlockIndex(block_hash) != nullptr;
}

void PeerManagerImpl::SendPings()
{
    LOCK(m_peer_mutex);
    for(auto& it : m_peer_map) it.second->m_ping_queued = true;
}

void PeerManagerImpl::RelayTransaction(const uint256& txid, const uint256& wtxid)
{
    WITH_LOCK(cs_main, _RelayTransaction(txid, wtxid););
}

void PeerManagerImpl::_RelayTransaction(const uint256& txid, const uint256& wtxid)
{
    m_connman.ForEachNode([&txid, &wtxid](CNode* pnode) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        AssertLockHeld(::cs_main);

        CNodeState* state = State(pnode->GetId());
        if (state == nullptr) return;
        if (state->m_wtxid_relay) {
            pnode->PushTxInventory(wtxid);
        } else {
            pnode->PushTxInventory(txid);
        }
    });
}

void PeerManagerImpl::RelayAddress(NodeId originator,
                                   const CAddress& addr,
                                   bool fReachable)
{
    // We choose the same nodes within a given 24h window (if the list of connected
    // nodes does not change) and we don't relay to nodes that already know an
    // address. So within 24h we will likely relay a given address once. This is to
    // prevent a peer from unjustly giving their address better propagation by sending
    // it to us repeatedly.

    if (!fReachable && !addr.IsRelayable()) return;

    // Relay to a limited number of other nodes
    // Use deterministic randomness to send to the same nodes for 24 hours
    // at a time so the m_addr_knowns of the chosen nodes prevent repeats
    const uint64_t hashAddr{addr.GetHash()};
    const CSipHasher hasher{m_connman.GetDeterministicRandomizer(RANDOMIZER_ID_ADDRESS_RELAY).Write(hashAddr).Write((GetTime() + hashAddr) / (24 * 60 * 60))};
    FastRandomContext insecure_rand;

    // Relay reachable addresses to 2 peers. Unreachable addresses are relayed randomly to 1 or 2 peers.
    unsigned int nRelayNodes = (fReachable || (hasher.Finalize() & 1)) ? 2 : 1;

    std::array<std::pair<uint64_t, Peer*>, 2> best{{{0, nullptr}, {0, nullptr}}};
    assert(nRelayNodes <= best.size());

    LOCK(m_peer_mutex);

    for (auto& [id, peer] : m_peer_map) {
        if (peer->m_addr_relay_enabled && id != originator && IsAddrCompatible(*peer, addr)) {
            uint64_t hashKey = CSipHasher(hasher).Write(id).Finalize();
            for (unsigned int i = 0; i < nRelayNodes; i++) {
                 if (hashKey > best[i].first) {
                     std::copy(best.begin() + i, best.begin() + nRelayNodes - 1, best.begin() + i + 1);
                     best[i] = std::make_pair(hashKey, peer.get());
                     break;
                 }
            }
        }
    };

    for (unsigned int i = 0; i < nRelayNodes && best[i].first != 0; i++) {
        PushAddress(*best[i].second, addr, insecure_rand);
    }
}

void PeerManagerImpl::ProcessGetBlockData(CNode& pfrom, Peer& peer, const CInv& inv)
{
    std::shared_ptr<const CBlock> a_recent_block;
    std::shared_ptr<const CBlockHeaderAndShortTxIDs> a_recent_compact_block;
    bool fWitnessesPresentInARecentCompactBlock;
    {
        LOCK(cs_most_recent_block);
        a_recent_block = most_recent_block;
        a_recent_compact_block = most_recent_compact_block;
        fWitnessesPresentInARecentCompactBlock = fWitnessesPresentInMostRecentCompactBlock;
    }

    bool need_activate_chain = false;
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = m_chainman.m_blockman.LookupBlockIndex(inv.hash);
        if (pindex) {
            if (pindex->HaveTxsDownloaded() && !pindex->IsValid(BLOCK_VALID_SCRIPTS) &&
                    pindex->IsValid(BLOCK_VALID_TREE)) {
                // If we have the block and all of its parents, but have not yet validated it,
                // we might be in the middle of connecting it (ie in the unlock of cs_main
                // before ActivateBestChain but after AcceptBlock).
                // In this case, we need to run ActivateBestChain prior to checking the relay
                // conditions below.
                need_activate_chain = true;
            }
        }
    } // release cs_main before calling ActivateBestChain
    if (need_activate_chain) {
        BlockValidationState state;
        if (!m_chainman.ActiveChainstate().ActivateBestChain(state, a_recent_block)) {
            LogPrint(BCLog::NET, "failed to activate chain (%s)\n", state.ToString());
        }
    }

    LOCK(cs_main);
    const CBlockIndex* pindex = m_chainman.m_blockman.LookupBlockIndex(inv.hash);
    if (!pindex) {
        return;
    }
    if (!BlockRequestAllowed(pindex)) {
        LogPrint(BCLog::NET, "%s: ignoring request from peer=%i for old block that isn't in the main chain\n", __func__, pfrom.GetId());
        return;
    }
    const CNetMsgMaker msgMaker(pfrom.GetCommonVersion());
    // disconnect node in case we have reached the outbound limit for serving historical blocks
    if (m_connman.OutboundTargetReached(true) &&
        (((pindexBestHeader != nullptr) && (pindexBestHeader->GetBlockTime() - pindex->GetBlockTime() > HISTORICAL_BLOCK_AGE)) || inv.IsMsgFilteredBlk()) &&
        !pfrom.HasPermission(NetPermissionFlags::Download) // nodes with the download permission may exceed target
    ) {
        LogPrint(BCLog::NET, "historical block serving limit reached, disconnect peer=%d\n", pfrom.GetId());
        pfrom.fDisconnect = true;
        return;
    }
    // Avoid leaking prune-height by never sending blocks below the NODE_NETWORK_LIMITED threshold
    if (!pfrom.HasPermission(NetPermissionFlags::NoBan) && (
            (((pfrom.GetLocalServices() & NODE_NETWORK_LIMITED) == NODE_NETWORK_LIMITED) && ((pfrom.GetLocalServices() & NODE_NETWORK) != NODE_NETWORK) && (m_chainman.ActiveChain().Tip()->nHeight - pindex->nHeight > (int)NODE_NETWORK_LIMITED_MIN_BLOCKS + 2 /* add two blocks buffer extension for possible races */) )
       )) {
        LogPrint(BCLog::NET, "Ignore block request below NODE_NETWORK_LIMITED threshold, disconnect peer=%d\n", pfrom.GetId());
        //disconnect node and prevent it from stalling (would otherwise wait for the missing block)
        pfrom.fDisconnect = true;
        return;
    }
    // Pruned nodes may have deleted the block, so check whether
    // it's available before trying to send.
    if (!(pindex->nStatus & BLOCK_HAVE_DATA)) {
        return;
    }
    std::shared_ptr<const CBlock> pblock;
    if (a_recent_block && a_recent_block->GetHash() == pindex->GetBlockHash()) {
        pblock = a_recent_block;
    } else if (inv.IsMsgWitnessBlk()) {
        // Fast-path: in this case it is possible to serve the block directly from disk,
        // as the network format matches the format on disk
        std::vector<uint8_t> block_data;
        if (!ReadRawBlockFromDisk(block_data, pindex, m_chainparams.MessageStart())) {
            assert(!"cannot load block from disk");
        }
        m_connman.PushMessage(&pfrom, msgMaker.Make(NetMsgType::BLOCK, Span{block_data}));
        // Don't set pblock as we've sent the block
    } else {
        // Send block from disk
        std::shared_ptr<CBlock> pblockRead = std::make_shared<CBlock>();
        if (!ReadBlockFromDisk(*pblockRead, pindex, m_chainparams.GetConsensus())) {
            assert(!"cannot load block from disk");
        }
        pblock = pblockRead;
    }
    if (pblock) {
        if (inv.IsMsgBlk()) {
            m_connman.PushMessage(&pfrom, msgMaker.Make(SERIALIZE_TRANSACTION_NO_WITNESS, NetMsgType::BLOCK, *pblock));
        } else if (inv.IsMsgWitnessBlk()) {
            m_connman.PushMessage(&pfrom, msgMaker.Make(NetMsgType::BLOCK, *pblock));
        } else if (inv.IsMsgFilteredBlk()) {
            bool sendMerkleBlock = false;
            CMerkleBlock merkleBlock;
            if (pfrom.m_tx_relay != nullptr) {
                LOCK(pfrom.m_tx_relay->cs_filter);
                if (pfrom.m_tx_relay->pfilter) {
                    sendMerkleBlock = true;
                    merkleBlock = CMerkleBlock(*pblock, *pfrom.m_tx_relay->pfilter);
                }
            }
            if (sendMerkleBlock) {
                m_connman.PushMessage(&pfrom, msgMaker.Make(NetMsgType::MERKLEBLOCK, merkleBlock));
                // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                // This avoids hurting performance by pointlessly requiring a round-trip
                // Note that there is currently no way for a node to request any single transactions we didn't send here -
                // they must either disconnect and retry or request the full block.
                // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                // however we MUST always provide at least what the remote peer needs
                typedef std::pair<unsigned int, uint256> PairType;
                for (PairType& pair : merkleBlock.vMatchedTxn)
                    m_connman.PushMessage(&pfrom, msgMaker.Make(SERIALIZE_TRANSACTION_NO_WITNESS, NetMsgType::TX, *pblock->vtx[pair.first]));
            }
            // else
            // no response
        } else if (inv.IsMsgCmpctBlk()) {
            // If a peer is asking for old blocks, we're almost guaranteed
            // they won't have a useful mempool to match against a compact block,
            // and we don't feel like constructing the object for them, so
            // instead we respond with the full, non-compact block.
            bool fPeerWantsWitness = State(pfrom.GetId())->fWantsCmpctWitness;
            int nSendFlags = fPeerWantsWitness ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;
            if (CanDirectFetch() && pindex->nHeight >= m_chainman.ActiveChain().Height() - MAX_CMPCTBLOCK_DEPTH) {
                if ((fPeerWantsWitness || !fWitnessesPresentInARecentCompactBlock) && a_recent_compact_block && a_recent_compact_block->header.GetHash() == pindex->GetBlockHash()) {
                    m_connman.PushMessage(&pfrom, msgMaker.Make(nSendFlags, NetMsgType::CMPCTBLOCK, *a_recent_compact_block));
                } else {
                    CBlockHeaderAndShortTxIDs cmpctblock(*pblock, fPeerWantsWitness);
                    m_connman.PushMessage(&pfrom, msgMaker.Make(nSendFlags, NetMsgType::CMPCTBLOCK, cmpctblock));
                }
            } else {
                m_connman.PushMessage(&pfrom, msgMaker.Make(nSendFlags, NetMsgType::BLOCK, *pblock));
            }
        }
    }

    {
        LOCK(peer.m_block_inv_mutex);
        // Trigger the peer node to send a getblocks request for the next batch of inventory
        if (inv.hash == peer.m_continuation_block) {
            // Send immediately. This must send even if redundant,
            // and we want it right after the last block so they don't
            // wait for other stuff first.
            std::vector<CInv> vInv;
            vInv.push_back(CInv(MSG_BLOCK, m_chainman.ActiveChain().Tip()->GetBlockHash()));
            m_connman.PushMessage(&pfrom, msgMaker.Make(NetMsgType::INV, vInv));
            peer.m_continuation_block.SetNull();
        }
    }
}

CTransactionRef PeerManagerImpl::FindTxForGetData(const CNode& peer, const GenTxid& gtxid, const std::chrono::seconds mempool_req, const std::chrono::seconds now)
{
    auto txinfo = m_mempool.info(gtxid);
    if (txinfo.tx) {
        // If a TX could have been INVed in reply to a MEMPOOL request,
        // or is older than UNCONDITIONAL_RELAY_DELAY, permit the request
        // unconditionally.
        if ((mempool_req.count() && txinfo.m_time <= mempool_req) || txinfo.m_time <= now - UNCONDITIONAL_RELAY_DELAY) {
            return std::move(txinfo.tx);
        }
    }

    {
        LOCK(cs_main);
        // Otherwise, the transaction must have been announced recently.
        if (State(peer.GetId())->m_recently_announced_invs.contains(gtxid.GetHash())) {
            // If it was, it can be relayed from either the mempool...
            if (txinfo.tx) return std::move(txinfo.tx);
            // ... or the relay pool.
            auto mi = mapRelay.find(gtxid.GetHash());
            if (mi != mapRelay.end()) return mi->second;
        }
    }

    return {};
}

void Pee