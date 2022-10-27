#!/usr/bin/env python3
# Copyright (c) 2015-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test processing of unrequested blocks.

Setup: two nodes, node0 + node1, not connected to each other. Node1 will have
nMinimumChainWork set to 0x10, so it won't process low-work unrequested blocks.

We have one P2PInterface connection to node0 called test_node, and one to node1
called min_work_node.

The test:
1. Generate one block on each node, to leave IBD.

2. Mine a new block on each tip, and deliver to each node from node's peer.
   The tip should advance for node0, but node1 should skip processing due to
   nMinimumChainWork.

Node1 is unused in tests 3-7:

3. Mine a block that forks from the genesis block, and deliver to test_node.
   Node0 should not process this block (just accept the header), because it
   is unrequested and doesn't have more or equal work to the tip.

4a,b. Send another two blocks that build on the forking block.
   Node0 should process the second block but be stuck on the shorter chain,
   because it's missing an intermediate block.

4c.Send 288 more blocks on the longer chain (the number of blocks ahead
   we currently store).
   Node0 should process all but the last block (too far ahead in height).

5. Send a duplicate of the block in #3 to Node0.
   Node0 should not process the block because it is unrequested, and stay on
   the shorter chain.

6. Send Node0 an inv for the height 3 block produced in #4 above.
   Node0 should figure out that Node0 has the missing height 2 block and send a
   getdata.

7. Send Node0 the missing block again.
   Node0 should process and the tip should advance.

8. Create a fork which is invalid at a height longer than the current chain
   (ie to which the node will try to reorg) but which has headers built on top
   of the invalid block. Check that we get disconnected if we send more headers
   on the chain the node now knows to be invalid.

9. Test Node1 is able to sync when connected to node0 (which should have sufficient
   work on its chain).
"""

import time

from test_framework.blocktools import create_block, create_coinbase, create_tx_with_script
from test_framework.messages import CBlockHeader, CInv, MSG_BLOCK, msg_block, msg_headers, msg_inv
from test_framework.p2p import p2p_lock, P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class AcceptBlockTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [[], ["-minimumchainwork=0x10"]]

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        test_node = self.nodes[0].add_p2p_connection(P2PInterface())
        min_work_node = self.nodes[1].add_p2p_connection(P2PInterface())

        # 1. Have nodes mine a block (leave IBD)
        [self.generate(n, 1, sync_fun=self.no_op) for n in self.nodes]
        tips = [int("0x" + n.getbestblockhash(), 0) for n in self.nodes]

        # 2. Send one block that builds on each tip.
        # This should be accepted by node0
        blocks_h2 = []  # the height 2 blocks on each node's chain
        block_time = int(time.time()) + 1
        for i in ra