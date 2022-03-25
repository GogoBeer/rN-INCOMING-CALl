// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txrequest.h>

#include <crypto/siphash.h>
#include <net.h>
#include <primitives/transaction.h>
#include <random.h>
#include <uint256.h>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <chrono>
#include <unordered_map>
#include <utility>

#include <assert.h>

namespace {

/** The various states a (txhash,peer) pair can be in.
 *
 * Note that CANDIDATE is split up into 3 substates (DELAYED, BEST, READY), allowing more efficient implementation.
 * Also note that the sorting order of ByTxHashView relies on the specific order of values in this enum.
 *
 * Expected behaviour is:
 *   - When first announced by a peer, the state is CANDIDATE_DELAYED until reqtime is reached.
 *   - Announcements that have reached their reqtime but not been requested will be either CANDIDATE_READY or
 *     CANDIDATE_BEST. Neither of those has an expiration time; they remain in that state until they're requested or
 *     no longer needed. CANDIDATE_READY announcements are promoted to CANDIDATE_BEST when they're the best one left.
 *   - When requested, an announcement will be in state REQUESTED until expiry is reached.
 *   - If expiry is reached, or the peer replies to the request (either with NOTFOUND or the tx), the state becomes
 *     COMPLETED.
 */
enum class State : uint8_t {
    /** A CANDIDATE announcement whose reqtime is in the future. */
    CANDIDATE_DELAYED,
    /** A CANDIDATE announcement that's not CANDIDATE_DELAYED or CANDIDATE_BEST. */
    CANDIDATE_READY,
    /** The best CANDIDATE for a given txhash; only if there is no REQUESTED announcement already for that txhash.
     *  The CANDIDATE_BEST is the highest-priority announcement among all CANDIDATE_READY (and _BEST) ones for that
     *  txhash. */
    CANDIDATE_BEST,
    /** A REQUESTED announcement. */
    REQUESTED,
    /** A COMPLETED announcement. */
    COMPLETED,
};

//! Type alias for sequence numbers.
using SequenceNumber = uint64_t;

/** An announcement. This is the data we track for each txid or wtxid that is announced to us by each peer. */
struct Announcement {
    /** Txid or wtxid that was announced. */
    const uint256 m_txhash;
    /** For CANDIDATE_{DELAYED,BEST,READY} the reqtime; for REQUESTED the expiry. */
    std::chrono::microseconds m_time;
    /** What peer the request was from. */
    const NodeId m_peer;
    /** What sequence number this announcement has. */
    const SequenceNumber m_sequence : 59;
    /** Whether the request is preferred. */
    const bool m_preferred : 1;
    /** Whether this is a wtxid request. */
    const bool m_is_wtxid : 1;

    /** What state this announcement is in.
     *  This is a uint8_t instead of a State to silence a GCC warning in versions prior to 8.4 and 9.3.
     *  See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61414 */
    uint8_t m_state : 3;

    /** Convert m_state to a State enum. */
    State GetState() const { return static_cast<State>(m_state); }

    /** Convert a State enum to a uint8_t and store it in m_state. */
    void SetState(State state) { m_state = static_cast<uint8_t>(state); }

    /** Whether this announcement is selected. There can be at most 1 selected peer per txhash. */
    bool IsSelected() const
    {
        return GetState() == State::CANDIDATE_BEST || GetState() == State::REQUESTED;
    }

    /** Whether this announcement is waiting for a certain time to pass. */
    bool IsWaiting() const
    {
        return GetState() == State::REQUESTED || GetState() == State::CANDIDATE_DELAYED;
    }

    /** Whether this announcement can feasibly be selected if the current IsSelected() one disappears. */
    bool IsSelectable() const
    {
        return GetState() == State::CANDIDATE_READY || GetState() == State::CANDIDATE_BEST;
    }

    /** Construct a new announcement from scratch, initially in CANDIDATE_DELAYED state. */
    Announcement(const GenTxid& gtxid, NodeId peer, bool preferred, std::chrono::microseconds reqtime,
        SequenceNumber sequence) :
        m_txhash(gtxid.GetHash()), m_time(reqtime), m_peer(peer), m_sequence(sequence), m_preferred(preferred),
        m_is_wtxid(gtxid.IsWtxid()), m_state(static_cast<uint8_t>(State::CANDIDATE_DELAYED)) {}
};

//! Type alias for priorities.
using Priority = uint64_t;

/** A functor with embedded salt that computes priority of an announcement.
 *
 * Higher priorities are selected first.
 */
class PriorityComputer {
    const uint64_t m_k0, m_k1;
public:
    explicit PriorityComputer(bool deterministic) :
        m_k0{deterministic ? 0 : GetRand(0xFFFFFFFFFFFFFFFF)},
        m_k1{deterministic ? 0 : GetRand(0xFFFFFFFFFFFFFFFF)} {}

    Priority operator()(const uint256& txhash, NodeId peer, bool preferred) const
    {
        uint64_t low_bits = CSipHasher(m_k0, m_k1).Write(txhash.begin(), txhash.size()).Write(peer).Finalize() >> 1;
        return low_bits | uint64_t{preferred} << 63;
    }

    Priority operator()(const Announcement& ann) const
    {
        return operator()(ann.m_txhash, ann.m_peer, ann.m_preferred);
    }
};

// Definitions for the 3 indexes used in the main data structure.
//
// Each index has a By* type to identify it, a By*View data type to represent the view of announcement it is sorted
// by, and an By*ViewExtractor type to convert an announcement into the By*View type.
// See https://www.boost.org/doc/libs/1_58_0/libs/multi_index/doc/reference/key_extraction.html#key_extractors
// for more information about the key extraction concept.

// The ByPeer index is sorted by (peer, state == CANDIDATE_BEST, txhash)
//
// Uses:
// * Looking up existing announcements by peer/txhash, by checking both (peer, false, txhash) and
//   (peer, true, txhash).
// * Finding all CANDIDATE_BEST announcements for a given peer in GetRequestable.
struct ByPeer {};
using ByPeerView = std::tuple<NodeId, bool, const uint256&>;
struct ByPeerViewExtractor
{
    using result_type = ByPeerView;
    result_type operator()(const Announcement& ann) const
    {
        return ByPeerView{ann.m_peer, ann.GetState() == State::CANDIDATE_BEST, ann.m_txhash};
    }
};

// The ByTxHash index is sorted by (txhash, state, priority).
//
// Note: priority == 0 whenever state != CANDIDATE_READY.
//
// Uses:
// * Deleting all announcements with a given txhash in ForgetTxHash.
// * Finding the best CANDIDATE_READY to convert to CANDIDATE_BEST, when no other CANDIDATE_READY or REQUESTED
//   announcement exists for that txhash.
// * Determining when no more non-COMPLETED announcements for a given txhash exist, so the COMPLETED ones can be
//   deleted.
struct ByTxHash {};
using ByTxHashView = std::tuple<const uint256&, State, Priority>;
class ByTxHashViewExtractor {
    const PriorityComputer& m_computer;
public:
    explicit ByTxHashViewExtractor(const PriorityComputer& computer) : m_computer(computer) {}
    using result_type = ByTxHashView;
    result_type operator()(const Announcement& ann) const
    {
        const Priority prio = (ann.GetState() == State::CANDIDATE_READY) ? m_computer(ann) : 0;
        return ByTxHashView{ann.m_txhash, ann.GetState(), prio};
    }
};

enum class WaitState {
    //! Used for announcements that need efficient testing of "is their timestamp in the future?".
    FUTURE_EVENT,
    //! Used for announcements whose timestamp is not relevant.
    NO_EVENT,
    //! Used for announcements that need efficient testing of "is their timestamp in the past?".
    PAST_EVENT,
};

WaitState GetWaitState(const Announcement& ann)
{
    if (ann.IsWaiting()) return WaitState::FUTURE_EVENT;
    if (ann.IsSelectable()) return WaitState::PAST_EVENT;
    return WaitState::NO_EVENT;
}

// The ByTime index is sorted by (wait_state, time).
//
// All announcements with a timestamp in the future can be found by iterating the index forward from the beginning.
// All announcements with a timestamp in the past can be found by iterating the index backwards from the end.
//
// Uses:
// * Finding CANDIDATE_DELAYED announcements whose reqtime has passed, and REQUESTED announcements whose expiry has
//   passed.
// * Finding CANDIDATE_READY/BEST announcements whose reqtime is in the future (when the clock time went backwards).
struct ByTime {};
using ByTimeView = std::pair<WaitState, std::chrono::microseconds>;
struct ByTimeViewExtractor
{
    using result_type = ByTimeView;
    result_type operator()(const Announcement& ann) const
    {
        return ByTimeView{GetWaitState(ann), ann.m_time};
    }
};

/** Data type for the main data structure (Announcement objects with ByPeer/ByTxHash/ByTime indexes). */
using Index = boost::multi_index_container<
    Announcement,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<boost::multi_index::tag<ByPeer>, ByPeerViewExtractor>,
        boost::multi_index::ordered_non_unique<boost::multi_index::tag<ByTxHash>, ByTxHashViewExtractor>,
        boost::multi_index::ordered_non_unique<boost::multi_index::tag<ByTime>, ByTimeViewExtractor>
    >
>;

/** Helper type to simplify syntax of iterator types. */
template<typename Tag>
using Iter = typename Index::index<Tag>::type::iterator;

/** Per-peer statistics object. */
struct PeerInfo {
    size_t m_total = 0; //!< Total number of announcements for this peer.
    size_t m_completed = 0; //!< Number of COMPLETED announcements for this peer.
    size_t m_requested = 0; //!< Number of REQUESTED announcements for this peer.
};

/** Per-txhash statistics object. Only used for sanity checking. */
struct TxHashInfo
{
    //! Number of CANDIDATE_DELAYED announcements for this txhash.
    size_t m_candidate_delayed = 0;
    //! Number of CANDIDATE_READY announcements for this txhash.
    size_t m_candidate_ready = 0;
    //! Number of CANDIDATE_BEST announcements for this txhash (at most one).
    size_t m_candidate_best = 0;
    //! Number of REQUESTED announcements for this txhash (at most one; mutually exclusive with CANDIDATE_BEST).
    size_t m_requested = 0;
    //! The priority of the CANDIDATE_BEST announcement if one exists, or max() otherwise.
    Priority m_priority_candidate_best = std::numeric_limits<Priority>::max();
    //! The highest priority of all CANDIDATE_READY announcements (or min() if none exist).
    Priority m_priority_best_candidate_ready = std::numeric_limits<Priority>::min();
    //! All peers we have an announcement for this txhash for.
    std::vector<NodeId> m_peers;
};

/** Compare two PeerInfo objects. Only used for sanity checking. */
bool operator==(const PeerInfo& a, const PeerInfo& b)
{
    return std::tie(a.m_total, a.m_completed, a.m_requested) ==
           std::tie(b.m_total, b.m_completed, b.m_requested);
};

/** (Re)compute the PeerInfo map from the index. Only used for sanity checking. */
std::unordered_map<NodeId, PeerInfo> RecomputePeerInfo(const Index& index)
{
    std::unordered_map<NodeId, PeerInfo> ret;
    for (const Announcement& ann : index) {
        PeerInfo& info = ret[ann.m_peer];
        ++info.m_total;
        info.m_requested += (ann.GetState() == State::REQUESTED);
        info.m_completed += (ann.GetState() == State::COMPLETED);
    }
    return ret;
}

/** Compute the TxHashInfo map. Only used for sanity checking. */
std::map<uint256, TxHashInfo> ComputeTxHashInfo(const Index& index, const PriorityComputer& computer)
{
    std::map<uint256, TxHashInfo> ret;
    for (const Announcement& ann : index) {
        TxHashInfo& info = ret[ann.m_txhash];
        // Classify how many announcements of each state we have for this txhash.
        info.m_candidate_delayed += (ann.GetState() == State::CANDIDATE_DELAYED);
        info.m_candidate_ready += (ann.GetState() == State::CANDIDATE_READY);
        info.m_candidate_best += (ann.GetState() == State::CANDIDATE_BEST);
        info.m_requested += (ann.GetState() == State::REQUESTED);
        // And track the priority of the best CANDIDATE_READY/CANDIDATE_BEST announcements.
        if (ann.GetState() == State::CANDIDATE_BEST) {
            info.m_priority_candidate_best = computer(ann);
        }
        if (ann.GetState() == State::CANDIDATE_READY) {
            info.m_priority_best_candidate_ready = std::max(info.m_priority_best_candidate_ready, computer(ann));
        }
        // Also keep track of which peers this txhash has an announcement for (so we can detect duplicates).
        info.m_peers.push_back(ann.m_peer);
    }
    return ret;
}

GenTxid ToGenTxid(const Announcement& ann)
{
    return ann.m_is_wtxid ? GenTxid::Wtxid(ann.m_txhash) : GenTxid::Txid(ann.m_txhash);
}

}  // namespace

/** Actual implementation for TxRequestTracker's data structure. */
class TxRequestTracker::Impl {
    //! The current sequence number. Increases for every announcement. This is used to sort txhashes returned by
    //! GetRequestable in announcement order.
    SequenceNumber m_current_sequence{0};

    //! This tracker's priority computer.
    const PriorityComputer m_computer;

    //! This tracker's main data structure. See SanityCheck() for the invariants that apply to it.
    Index m_index;

    //! Map with this tracker's per-peer statistics.
    std::unordered_map<NodeId, PeerInfo> m_peerinfo;

public:
    void SanityCheck() const
    {
        // Recompute m_peerdata from m_index. This verifies the data in it as it should just be caching statistics
        // on m_index. It also verifies the invariant that no PeerInfo announcements with m_total==0 exist.
        assert(m_peerinfo == RecomputePeerInfo(m_index));

        // Calculate per-txhash statistics from m_index, and validate invariants.
        for (auto& item : ComputeTxHashInfo(m_index, m_computer)) {
            TxHashInfo& info = item.second;

            // Cannot have only COMPLETED peer (txhash should have been forgotten already)
            assert(info.m_candidate_delayed + info.m_candidate_ready + info.m_candidate_best + info.m_requested > 0);

            // Can have at most 1 CANDIDATE_BEST/REQUESTED peer
            assert(info.m_candidate_best + info.m_requested <= 1);

            // If there are any CANDIDATE_READY announcements, there must be exactly one CANDIDATE_BEST or REQUESTED
            // announcement.
            if (info.m_candidate_ready > 0) {
                assert(info.m_candidate_best + info.m_requested == 1);
            }

            // If there is both a CANDIDATE_READY and a CANDIDATE_BEST announcement, the CANDIDATE_BEST one must be
            // at least as good (equal or higher priority) as the best CANDIDATE_READY.
            if (info.m_candidate_ready && info.m_candidate_best) {
                assert(info.m_priority_candidate_best >= info.m_priority_best_candidate_ready);
            }

            // No txhash can have been announced by the same peer twice.
            std::sort(info.m_peers.begin(), info.m_peers.end());
            assert(std::adjacent_find(info.m_peers.begin(), info.m_peers.end()) == info.m_peers.end());
        }
    }

    void PostGetRequestableSanityCheck(std::chrono::microseconds now) const
    {
        for (const Announcement& ann : m_index) {
            if (ann.IsWaiting()) {
                // REQUESTED and CANDIDATE_DELAYED must have a time in the future (they should have been converted
                // to COMPLETED/CANDIDA