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

        # Wait for blk00000.dat to be pruned
        self.wait_until(lambda: not os.path.isfile(os.path.join(self.prunedir, "blk00000.dat")), timeout=30)

        self.log.info("Success")
        usage = calc_usage(self.prunedir)
        self.log.info(f"Usage should be below target: {usage}")
        assert_greater_than(550, usage)

    def create_chain_with_staleblocks(self):
        # Create stale blocks in manageable sized chunks
        self.log.info("Mine 24 (stale) blocks on Node 1, followed by 25 (main chain) block reorg from Node 0, for 12 rounds")

        for _ in range(12):
            # Disconnect node 0 so it can mine a longer reorg chain without knowing about node 1's soon-to-be-stale chain
            # Node 2 stays connected, so it hears about the stale blocks and then reorg's when node0 reconnects
            self.disconnect_nodes(0, 1)
            self.disconnect_nodes(0, 2)
            # Mine 24 blocks in node 1
            mine_large_blocks(self.nodes[1], 24)

            # Reorg back with 25 block chain from node 0
            mine_large_blocks(self.nodes[0], 25)

            # Create connections in the order so both nodes can see the reorg at the same time
            self.connect_nodes(0, 1)
            self.connect_nodes(0, 2)
            self.sync_blocks(self.nodes[0:3])

        self.log.info(f"Usage can be over target because of high stale rate: {calc_usage(self.prunedir)}")

    def reorg_test(self):
        # Node 1 will mine a 300 block chain starting 287 blocks back from Node 0 and Node 2's tip
        # This will cause Node 2 to do a reorg requiring 288 blocks of undo data to the reorg_test chain

        height = self.nodes[1].getblockcount()
        self.log.info(f"Current block height: {height}")

        self.forkheight = height - 287
        self.forkhash = self.nodes[1].getblockhash(self.forkheight)
        self.log.info(f"Invalidating block {self.forkhash} at height {self.forkheight}")
        self.nodes[1].invalidateblock(self.forkhash)

        # We've now switched to our previously mined-24 block fork on node 1, but that's not what we want
        # So invalidate that fork as well, until we're on the same chain as node 0/2 (but at an ancestor 288 blocks ago)
        mainchainhash = self.nodes[0].getblockhash(self.forkheight - 1)
        curhash = self.nodes[1].getblockhash(self.forkheight - 1)
        while curhash != mainchainhash:
            self.nodes[1].invalidateblock(curhash)
            curhash = self.nodes[1].getblockhash(self.forkheight - 1)

        assert self.nodes[1].getblockcount() == self.forkheight - 1
        self.log.info(f"New best height: {self.nodes[1].getblockcount()}")

        # Disconnect node1 and generate the new chain
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(1, 2)

        self.log.info("Generating new longer chain of 300 more blocks")
        self.generate(self.nodes[1], 300, sync_fun=self.no_op)

        self.log.info("Reconnect nodes")
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.sync_blocks(self.nodes[0:3], timeout=120)

        self.log.info(f"Verify height on node 2: {self.nodes[2].getblockcount()}")
        self.log.info(f"Usage possibly still high because of stale blocks in block files: {calc_usage(self.prunedir)}")

        self.log.info("Mine 220 more large blocks so we have requisite history")

        mine_large_blocks(self.nodes[0], 220)
        self.sync_blocks(self.nodes[0:3], timeout=120)

        usage = calc_usage(self.prunedir)
        self.log.info(f"Usage should be below target: {usage}")
        assert_greater_than(550, usage)

    def reorg_back(self):
        # Verify that a block on the old main chain fork has been pruned away
        assert_raises_rpc_error(-1, "Block not available (pruned data)", self.nodes[2].getblock, self.forkhash)
        with self.nodes[2].assert_debug_log(expected_msgs=['block verification stopping at height', '(pruning, no data)']):
            self.nodes[2].verifychain(checklevel=4, nblocks=0)
        self.log.info(f"Will need to redownload block {self.forkheight}")

        # Verify that we have enough history to reorg back to the fork point
        # Although this is more than 288 blocks, because this chain was written more recently
        # and only its other 299 small and 220 large blocks are in the block files after it,
        # it is expected to still be retained
        self.nodes[2].getblock(self.nodes[2].getblockhash(self.forkheight))

        first_reorg_height = self.nodes[2].getblockcount()
        curchainhash = self.nodes[2].getblockhash(self.mainchainheight)
        self.nodes[2].invalidateblock(curchainhash)
        goalbestheight = self.mainchainheight
        goalbesthash = self.mainchainhash2

        # As of 0.10 the current block download logic is not able to reorg to the original chain created in
        # create_chain_with_stale_blocks because it doesn't know of any peer that's on that chain from which to
        # redownload its missing blocks.
        # Invalidate the reorg_test chain in node 0 as well, it can successfully switch to the original chain
        # because it has all the block data.
        # However it must mine enough blocks to have a more work chain than the reorg_test chain in order
        # to trigger node 2's block download logic.
        # At this point node 2 is within 288 blocks of the fork point so it will preserve its ability to reorg
        if self.nodes[2].getblockcount() < self.mainchainheight:
            blocks_to_mine = first_reorg_height + 1 - self.mainchainheight
            self.log.info(f"Rewind node 0 to prev main chain to mine longer chain to trigger redownload. Blocks needed: {blocks_to_mine}")
            self.nodes[0].invalidateblock(curchainhash)
            assert_equal(self.nodes[0].getblockcount(), self.mainchainheight)
            assert_equal(self.nodes[0].getbestblockhash(), self.mainchainhash2)
            goalbesthash = self.generate(self.nodes[0], blocks_to_mine, sync_fun=self.no_op)[-1]
            goalbestheight = first_reorg_height + 1

        self.log.info("Verify node 2 reorged back to the main chain, some blocks of which it had to redownload")
        # Wait for Node 2 to reorg to proper height
        self.wait_until(lambda: self.nodes[2].getblockcount() >= goalbestheight, timeout=900)
        assert_equal(self.nodes[2].getbestblockhash(), goalbesthash)
        # Verify we can now have the data for a block previously pruned
        assert_equal(self.nodes[2].getblock(self.forkhash)["height"], self.forkheight)

    def manual_test(self, node_number, use_timestamp):
        # at this point, node has 995 blocks and has not yet run in prune mode
        self.start_node(node_number)
        node = self.nodes[node_number]
        assert_equal(node.getblockcount(), 995)
        assert_raises_rpc_error(-1, "Cannot prune blocks because node is not in prune mode", node.pruneblockchain, 500)

        # now re-start in manual pruning mode
        self.restart_node(node_number, extra_args=["-prune=1"])
        node = self.nodes[node_number]
        assert_equal(node.getblockcount(), 995)

        def height(index):
            if use_timestamp:
                return node.getblockheader(node.getblockhash(index))["time"] + TIMESTAMP_WINDOW
            else:
                return index

        def prune(index):
            ret = node.pruneblockchain(height=height(index))
            assert_equal(ret, node.getblockchaininfo()['pruneheight'])

        def has_block(index):
            return os.path.isfile(os.path.join(self.nodes[node_number].datadir, self.chain, "blocks", f"blk{index:05}.dat"))

        # should not prune because chain tip of node 3 (995) < PruneAfterHeight (1000)
        assert_raises_rpc_error(-1, "Blockchain is too short for pruning", node.pruneblockchain, height(500))

        # Save block transa