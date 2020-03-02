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
   