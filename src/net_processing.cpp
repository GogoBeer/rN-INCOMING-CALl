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
/** Time during which a peer must stall bl