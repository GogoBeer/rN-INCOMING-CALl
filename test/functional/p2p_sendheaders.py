#!/usr/bin/env python3
# Copyright (c) 2014-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test behavior of headers messages to announce blocks.

Setup:

- Two nodes:
    - node0 is the node-under-test. We create two p2p connections to it. The
      first p2p connection is a control and should only ever receive inv's. The
      second p2p connection tests the headers sending logic.
    - node1 is used to create reorgs.

test_null_locators
==================

Sends two getheaders requests with null locator values. First request's hashstop
value refers to validated block, while second request's hashstop value refers to
a block which hasn't been validated. Verifies only the first request returns
headers.

test_nonnull_locators
=====================

Part 1: No headers announcements before "sendheaders"
a. node mines a block [expect: inv]
   send getdata for the block [expect: block]
b. node mines another block [expect: inv]
   send getheaders and getdata [expect: headers, then block]
c. node mines another block [expect: inv]
   peer mines a block, announces with header [expect: getdata]
d. node mines another block [expect: inv]

Part 2: After "sendheaders", headers announcements should generally work.
a. peer sends sendheaders [expect: no response]
   peer sends getheaders with current tip [expect: no response]
b. node mines a block [expect: tip header]
c. for N in 1, ..., 10:
   * for announce-type in {inv, header}
     - peer mines N blocks, announces with announce-type
       [ expect: getheaders/getdata or getdata, deliver block(s) ]
     - node mines a block [ expect: 1 header ]

Part 3: Headers announcements stop after large reorg and resume after getheaders or inv from peer.
- For response-type in {inv, getheaders}
  * node mines a 7 block reorg [ expect: headers announcement of 8 blocks ]
  * node mines an 8-block reorg [ expect: inv at tip ]
  * peer responds with getblocks/getdata [expect: inv, blocks ]
  * node mines another block [ expect: inv at tip, peer sends getdata, expect: block ]
  * node mines another block at tip [ expect: inv ]
  * peer responds with getheaders with an old hashstop more than 8 blocks back [expect: headers]
  * peer requests block [ expect: block ]
  * node mines another block at tip [ expect: inv, peer sends getdata, expect: block ]
  * peer sends response-type [expect headers if getheaders, getheaders/getdata if mining new block]
  * node mines 1 block [expect: 1 header, peer responds with getdata]

Part 4: Test direct fetch behavior
a. Announce 2 old block headers.
   Expect: no getdata requests.
b. Announce 3 new blocks via 1 headers message.
   Expect: one getdata request for all 3 blocks.
   (Send blocks.)
c. Announce 1 header that forks off the last two blocks.
   Expect: no response.
d. Announce 1 more header that builds on that fork.
   Expect: one getdata request for two blocks.
e. Announce 16 more headers that build on that fork.
   Expect: getdata request for 14 more blocks.
f. Announce 1 more header that builds on that fork.
   Expect: no response.

Part 5: Test handling of headers that don't connect.
a. Repeat 10 times:
   1. Announce a header that doesn't connect.
      Expect: getheaders message
   2. Send headers chain.
      Expect: getdata for the missing blocks, tip update.
b. Then send 9 more headers that don't connect.
   Expect: getheaders message each time.
c. Announce a header that does connect.
   Expect: no response.
d. Announce 49 headers that don't connect.
   Expect: getheaders message each time.
e. Announce one more that doesn't connect.
   Expect: disconnect.
"""
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CInv
from test_framework.p2p import (
    CBlockHeader,
    NODE_WITNESS,
    P2PInterface,
    p2p_lock,
    MSG_BLOCK,
    msg_block,
    msg_getblocks,
    msg_getdata,
    msg_getheaders,
    msg_headers,
    msg_inv,
    msg_sendheaders,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)

DIRECT_FETCH_RESPONSE_TIME = 0.05

class BaseNode(P2PInterface):
    def __init__(self):
        super().__init__()

        self.block_announced = False
        self.last_blockhash_announced = None
        self.recent_headers_announced = []

    def send_get_data(self, block_hashes):
        """Request data for a list of block hashes."""
        msg = msg_getdata()
        for x in block_hashes:
            msg.inv.append(CInv(MSG_BLOCK, x))
        self.send_message(msg)

    def send_get_headers(self, locator, hashstop):
        msg = msg_getheaders()
        msg.locator.vHave = locator
        msg.hashstop = hashstop
        self.send_message(msg)

    def send_block_inv(self, blockhash):
        msg = msg_inv()
        msg.inv = [CInv(MSG_BLOCK, blockhash)]
        self.send_message(msg)

    def send_header_for_blocks(self, new_blocks):
        headers_message = msg_headers()
        headers_message.headers = [CBlockHeader(b) for b in new_blocks]
        self.send_message(headers_message)

    def send_getblocks(self, locator):
        getblocks_message = msg_getblocks()
        getblocks_message.locator.vHave = locator
        self.send_message(getblocks_message)

    def wait_for_block_announcement(self, block_hash, timeout=60):
        test_function = lambda: self.last_blockhash_announced == block_hash
        self.wait_until(test_function, timeout=timeout)

    def on_inv(self, message):
        self.block_announced = True
        self.last_blockhash_announced = message.inv[-1].hash

    def on_headers(self, message):
        if len(message.headers):
            self.block_announced = True
            for x in message.headers:
                x.calc_sha256()
                # append because headers may be announced over multiple messages.
                self.recent_headers_announced.append(x.sha256)
            self.last_blockhash_announced = message.headers[-1].sha256

    def clear_block_announcements(self):
        with p2p_lock:
            self.block_announced = False
            self.last_message.pop("inv", None)
            self.last_message.pop("headers", None)
            self.recent_headers_announced = []


    def check_last_headers_announcement(self, headers):
        """Test whether the last headers announcements received are right.
           Headers may be announced across more than one message."""
        test_function = lambda: (len(self.recent_headers_announced) >= len(headers))
        self.wait_until(test_function)
        with p2p_lock:
            assert_equal(self.recent_headers_announced, headers)
            self.block_announced = False
            self.last_message.pop("headers", None)
            self.recent_headers_announced = []

    def check_last_inv_announcement(self, inv):
        """Test whether the last announcement received had the right inv.
        inv should be a list of block hashes."""

        test_function = lambda: self.block_announced
        self.wait_until(test_function)

        with p2p_lock:
            compare_inv = []
            if "inv" in self.last_message:
                compare_inv = [x.hash for x in self.last_message["inv"].inv]
            assert_equal(compare_inv, inv)
            self.block_announced = False
            self.last_message.pop("inv", None)

class SendHeadersTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def mine_blocks(self, count):
        """Mine count blocks and return the new tip."""

        # Clear out block announcements from each p2p listener
        [x.clear_block_announcements() for x in self.nodes[0].p2ps]
        self.generatetoaddress(self.nodes[0], count, self.nodes[0].get_deterministic_priv_key().address)
        return int(self.nodes[0].getbestblockhash(), 16)

    def mine_reorg(self, length):
        """Mine a reorg that invalidates length blocks (replacing them with # length+1 blocks).

        Note: we clear the state of our p2p connections after the
        to-be-reorged-out blocks are mined, so that we don't break later tests.
        return the list of block hashes newly mined."""

        # make sure all invalidated blocks are node0's
        self.generatetoaddress(self.nodes[0], length, self.nodes[0].get_deterministic_priv_key().address)
        for x in self.nodes[0].p2ps:
            x.wait_for_block_announcement(int(self.nodes[0].getbestblockhash(), 16))
            x.clear_block_announcements()

        tip_height = self.nodes[1].getblockcount()
        hash_to_invalidate = self.nodes[1].getblockhash(tip_height - (length - 1))
        self.nodes[1].invalidateblock(hash_to_invalidate)
        all_hashes = self.generatetoaddress(self.nodes[1], length + 1, self.nodes[1].get_deterministic_priv_key().address)  # Must be longer than the orig chain
        return [int(x, 16) for x in all_hashes]

    def run_test(self):
        # Setup the p2p connections
        inv_node = self.nodes[0].add_p2p_connection(BaseNode())
        # Make sure NODE_NETWORK is not set for test_node, so no block download
        # will occur outside of direct fetching
        test_node = self.nodes[0].add_p2p_connection(BaseNode(), services=NODE_WITNESS)

        self.test_null_locators(test_node, inv_node)
        self.test_nonnull_locators(test_node, inv_node)

    def test_null_locators(self, test_node, inv_node):
        tip = self.nodes[0].getblockheader(self.generatetoaddress(self.nodes[0], 1, self.nodes[0].get_deterministic_priv_key().a