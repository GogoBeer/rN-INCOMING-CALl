// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <netaddress.h>
#include <net.h>
#include <test/util/net.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(net_peer_eviction_tests, BasicTestingSetup)

// Create `num_peers` random nodes, apply setup function `candidate_setup_fn`,
// call ProtectEvictionCandidatesByRatio() to apply protection logic, and then
// return true if all of `protected_peer_ids` and none of `unprotected_peer_ids`
// are protected from eviction, i.e. removed from the eviction candidates.
bool IsProtected(int num_peers,
                 std::function<void(NodeEvictionCandidate&)> candidate_setup_fn,
                 const std::unordered_set<NodeId>& protected_peer_ids,
                 const std::unordered_set<NodeId>& unprotected_peer_ids,
                 FastRandomContext& random_context)
{
    std::vector<NodeEvictionCandidate> candidates{GetRandomNodeEvictionCandidates(num_peers, random_context)};
    for (NodeEvictionCandidate& candidate : candidates) {
        candidate_setup_fn(candidate);
    }
    Shuffle(candidates.begin(), candidates.end(), random_context);

    const size_t size{candidates.size()};
    const size_t expected{size - size / 2}; // Expect half the candidates will be protected.
    ProtectEvictionCandidatesByRatio(candidates);
    BOOST_CHECK_EQUAL(candidates.size(), expected);

    size_t unprotected_count{0};
    for (const NodeEvictionCandidate& candidate : candidates) {
        if (protected_peer_ids.count(candidate.id)) {
            // this peer should have been removed from the eviction candidates
            BOOST_TEST_MESSAGE(strprintf("expected candidate to be protected: %d", candidate.id));
            return false;
        }
        if (unprotected_peer_ids.count(candidate.id)) {
            // this peer remains in the eviction candidates, as expected
            ++unprotected_count;
        }
    }

    const bool is_protected{unprotected_count == unprotected_peer_ids.size()};
    if (!is_protected) {
        BOOST_TEST_MESSAGE(strprintf("unprotected: expected %d, actual %d",
                                     unprotected_peer_ids.size(), unprotected_count));
    }
    return is_protected;
}

BOOST_AUTO_TEST_CASE(peer_protection_test)
{
    FastRandomContext random_context{true};
    int num_peers{12};

    // Expect half of the peers with greatest uptime (the lowest m_connected)
    // to be protected from eviction.
    BOOST_CHECK(IsProtected(
        num_peers, [](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{c.id};
            c.m_is_local = false;
            c.m_network = NET_IPV4;
        },
        /*protected_peer_ids=*/{0, 1, 2, 3, 4, 5},
        /*unprotected_peer_ids=*/{6, 7, 8, 9, 10, 11},
        random_context));

    // Verify in the opposite direction.
    BOOST_CHECK(IsProtected(
        num_peers, [num_peers](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{num_peers - c.id};
            c.m_is_local = false;
            c.m_network = NET_IPV6;
        },
        /*protected_peer_ids=*/{6, 7, 8, 9, 10, 11},
        /*unprotected_peer_ids=*/{0, 1, 2, 3, 4, 5},
        random_context));

    // Test protection of onion, localhost, and I2P peers...

    // Expect 1/4 onion peers to be protected from eviction,
    // if no localhost or I2P peers.
    BOOST_CHECK(IsProtected(
        num_peers, [](NodeEvictionCandidate& c) {
            c.m_is_local = false;
            c.m_network = (c.id == 3 || c.id == 8 || c.id == 9) ? NET_ONION : NET_IPV4;
        },
        /*protected_peer_ids=*/{3, 8, 9},
        /*unprotected_peer_ids=*/{},
        random_context));

    // Expect 1/4 onion peers and 1/4 of the other peers to be protected,
    // sorted by longest uptime (lowest m_connected), if no localhost or I2P peers.
    BOOST_CHECK(IsProtected(
        num_peers, [](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{c.id};
            c.m_is_local = false;
            c.m_network = (c.id == 3 || c.id > 7) ? NET_ONION : NET_IPV6;
        },
        /*protected_peer_ids=*/{0, 1, 2, 3, 8, 9},
        /*unprotected_peer_ids=*/{4, 5, 6, 7, 10, 11},
        random_context));

    // Expect 1/4 localhost peers to be protected from eviction,
    // if no onion or I2P peers.
    BOOST_CHECK(IsProtected(
        num_peers, [](NodeEvictionCandidate& c) {
            c.m_is_local = (c.id == 1 || c.id == 9 || c.id == 11);
            c.m_network = NET_IPV4;
        },
        /*protected_peer_ids=*/{1, 9, 11},
        /*unprotected_peer_ids=*/{},
        random_context));

    // Expect 1/4 localhost peers and 1/4 of the other peers to be protected,
    // sorted by longest uptime (lowest m_connected), if no onion or I2P peers.
    BOOST_CHECK(IsProtected(
        num_peers, [](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{c.id};
            c.m_is_local = (c.id > 6);
            c.m_network = NET_IPV6;
        },
        /*protected_peer_ids=*/{0, 1, 2, 7, 8, 9},
        /*unprotected_peer_ids=*/{3, 4, 5, 6, 10, 11},
        random_context));

    // Expect 1/4 I2P peers to be protected from eviction,
    // if no onion or localhost peers.
    BOOST_CHECK(IsProtected(
        num_peers, [](NodeEvictionCandidate& c) {
            c.m_is_local = false;
            c.m_network = (c.id == 2 || c.id == 7 || c.id == 10) ? NET_I2P : NET_IPV4;
        },
        /*protected_peer_ids=*/{2, 7, 10},
        /*unprotected_peer_ids=*/{},
        random_context));

    // Expect 1/4 I2P peers and 1/4 of the other peers to be protected,
    // sorted by longest uptime (lowest m_connected), if no onion or localhost peers.
    BOOST_CHECK(IsProtected(
        num_peers, [](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{c.id};
            c.m_is_local = false;
            c.m_network = (c.id == 4 || c.id > 8) ? NET_I2P : NET_IPV6;
        },
        /*protected_peer_ids=*/{0, 1, 2, 4, 9, 10},
        /*unprotected_peer_ids=*/{3, 5, 6, 7, 8, 11},
        random_context));

    // Tests with 2 networks...

    // Combined test: expect having 1 localhost and 1 onion peer out of 4 to
    // protect 1 localhost, 0 onion and 1 other peer, sorted by longest uptime;
    // stable sort breaks tie with array order of localhost first.
    BOOST_CHECK(IsProtected(
        4, [](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{c.id};
            c.m_is_local = (c.id == 4);
            c.m_network = (c.id == 3) ? NET_ONION : NET_IPV4;
        },
        /*protected_peer_ids=*/{0, 4},
        /*unprotected_peer_ids=*/{1, 2},
        random_context));

    // Combined test: expect having 1 localhost and 1 onion peer out of 7 to
    // protect 1 localhost, 0 onion, and 2 other peers (3 total), sorted by
    // uptime; stable sort breaks tie with array order of localhost first.
    BOOST_CHECK(IsProtected(
        7, [](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{c.id};
            c.m_is_local = (c.id == 6);
            c.m_network = (c.id == 5) ? NET_ONION : NET_IPV4;
        },
        /*protected_peer_ids=*/{0, 1, 6},
        /*unprotected_peer_ids=*/{2, 3, 4, 5},
        random_context));

    // Combined test: expect having 1 localhost and 1 onion peer out of 8 to
    // protect protect 1 localhost, 1 onion and 2 other peers (4 total), sorted
    // by uptime; stable sort breaks tie with array order of localhost first.
    BOOST_CHECK(IsProtected(
        8, [](NodeEvictionCandidate& c) {
            c.m_connected = std::chrono::seconds{c.id};
            c.m_is_local = (c.id == 6);
     