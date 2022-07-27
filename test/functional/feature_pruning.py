#!/usr/bin/env python3
# Copyright (c) 2014-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the pruning code.

WARNING:
This test uses 4GB of disk space.
This test takes 30 mins or more (up to 2 hours)
"""
import os

from test_framework.blocktools import create_coinbase
from test_framework.messages import CBlock
from test_framework.script import (
    CScript,
    OP_NOP,
    OP_RETURN,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)

# Rescans start at the earliest block up to 2 hours before a key timestamp, so
# the manual prune RPC avoids pruning blocks in the same window to be
# compatible with pruning based on key creation time.
TIMESTAMP_WINDOW = 2 * 60 * 60

def mine_large_blocks(node, n):
    # Make a large scriptPubKey for the coinbase transaction. This is OP_RETURN
    # followed by 950k of OP_NOP. This would be non-standard in a non-coinbase
    # transaction but is consensus valid.

    # Set the nTime if this is the first time this function has been called.
    # A static variable ensures that time is monotonicly increasing and is therefore
    # different for each block created => blockhash is unique.
    if "nTimes" not in mine_large_blocks.__dict__:
        mine_large_blocks.nTime = 0

    # Get the block parameters for the first block
    big_script = CScript([OP_RETURN] + [OP_NOP] * 950000)
    best_block = node.getblock(node.getbestblockhash())
    height = int(best_block["height"]) + 1
    mine_large_blocks.nTime = max(mine_large_blocks.nTime, int(best_block["time"])) + 1
    previousblockhash = int(best_block["hash"], 16)

    for _ in range(n):
        # Build the coinbase transaction (with large scriptPubKey)
        coinbase_tx = create_coinbase(height)
        coinbase_tx.vin[0].nSequence = 2 ** 32 - 1
        coinbase_tx.vout[0].scriptPubKey = big_script
        coinbase_tx.rehash()

        # Build the block
        block = CBlock()
        block.nVersion = best_block["version"]
        block.hashPrevBlock = previousblockhash
        block.nTime = mine_large_blocks.nTime
        block.nBits = int('207fffff', 16)
        block.nNonce = 0
        block.vtx = [coinbase_tx]
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        # Submit to the node
        node.submitblock(block.serialize().hex())

        previousblockhash = block.sha256
        height += 1
        mine_large_blocks.nTime += 1

def calc_usage(blockdir):
    return sum(os.path.getsize(blockdir + f) for f in os.listdir(blockdir) if os.path.isfile(os.path.join(blockdir, f))) / (1024. * 1024.)

class PruneTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6
        self.supports_cli = False

        # Create nodes 0 and 1 to mine.
        # Create node 2 to test pruning.
        self.full_node_default_args = ["-maxreceivebuffer=20000", "-checkblocks=5"]
        # Create nodes 3 and 4 to test manual pruning (they will be re-started with manual pruning later)
        # Create nodes 5 to test wallet in prune mode, but do not connect
        self.extra_args = [
            self.full_node_default_args,
            self.full_node_default_args,
            ["-maxreceivebuffer=20000", "-prune=550"],
            ["-maxreceivebuffer=20000"],
            ["-maxreceivebuffer=20000"],
            ["-prune=550"],
        ]
        self.rpc_timeout = 120

    def setup_network(self):
        self.setup_nodes()

        self.prunedir = os.path.join(self.nodes[2].datadir, self.chain, 'blocks', '')

        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.connect_nodes(0, 2)
        self.connect_nodes(0, 3)
        self.connect_nodes(0, 4)
        self.sync_blocks(self.nodes[0:5])

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args)
        self.start_nodes()
        if self.is_wallet_compiled():
            self.import_deterministic_coinbase_privkeys()

    def create_big_chain(self):
        # Start by creating some coinbases we can spend later
        self.generate(self.nodes[1], 200, sync_fun=lambda: self.sync_blocks(self.nodes[0:2]))
        self.generate(self.nodes[0], 150, sync_fun=self.no_op)

        # Then mine enough full blocks to create more than 550MiB of data
        mine_large_blocks(self.nodes[0], 645)

        self.sync_blocks(self.nodes[0:5])

    def test_invalid_command_line_options(self):
        self.nodes[0].assert_start_raises_init_error(
            expected_msg='Error: Prune cannot be configured with a negative value.',
            extra_args=['-prune=-1'],
        )
        self.nodes[0].assert_start_raises_init_error(
            expected_msg='Error: Prune configured below the minimum of 550 MiB.  Please use a higher number.',
            extra_args=['-prune=549'],
        )
        self.nodes[0].assert_start_raises_init_error(
            expected_msg='Error: Prune mode is incompatible with -txindex.',
            extra_args=['-prune=550', '-txindex'],
        )
        self.nodes[0].assert_start_raises_init_error(
            expected_msg='Error: Prune mode is incompatible with -coinstatsindex.',
            extra_args=['-prune=550', '-coinstatsindex'],
        )

    def test_height_min(self):
        assert os.path.isfile(os.path.join(self.prunedir, "blk00000.dat")), "blk00000.dat is missing, pruning too early"
        self.log.info("Success")
        self.log.info(f"Though we're already using more than 550MiB, current usage: {calc_usage(self.prunedir)}")
        self.log.info("Mining 25 more blocks should cause the first block file to be pruned")
        # Pruning doesn't run until we're allocating another chunk, 20 full blocks past the height cutoff will ensure this
        mine_large_blocks(self.nodes[0], 25)

        # Wait for bl