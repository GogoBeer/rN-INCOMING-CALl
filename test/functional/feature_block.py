
#!/usr/bin/env python3
# Copyright (c) 2015-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test block processing."""
import copy
import struct
import time

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    create_tx_with_script,
    get_legacy_sigopcount_block,
    MAX_BLOCK_SIGOPS,
)
from test_framework.key import ECKey
from test_framework.messages import (
    CBlock,
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    MAX_BLOCK_WEIGHT,
    uint256_from_compact,
    uint256_from_str,
)
from test_framework.p2p import P2PDataStore
from test_framework.script import (
    CScript,
    MAX_SCRIPT_ELEMENT_SIZE,
    OP_2DUP,
    OP_CHECKMULTISIG,
    OP_CHECKMULTISIGVERIFY,
    OP_CHECKSIG,
    OP_CHECKSIGVERIFY,
    OP_ELSE,
    OP_ENDIF,
    OP_DROP,
    OP_FALSE,
    OP_IF,
    OP_INVALIDOPCODE,
    OP_RETURN,
    OP_TRUE,
    SIGHASH_ALL,
    LegacySignatureHash,
)
from test_framework.script_util import (
    script_to_p2sh_script,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from data import invalid_txs

#  Use this class for tests that require behavior other than normal p2p behavior.
#  For now, it is used to serialize a bloated varint (b64).
class CBrokenBlock(CBlock):
    def initialize(self, base_block):
        self.vtx = copy.deepcopy(base_block.vtx)
        self.hashMerkleRoot = self.calc_merkle_root()

    def serialize(self, with_witness=False):
        r = b""
        r += super(CBlock, self).serialize()
        r += struct.pack("<BQ", 255, len(self.vtx))
        for tx in self.vtx:
            if with_witness:
                r += tx.serialize_with_witness()
            else:
                r += tx.serialize_without_witness()
        return r

    def normal_serialize(self):
        return super().serialize()


DUPLICATE_COINBASE_SCRIPT_SIG = b'\x01\x78'  # Valid for block at height 120


class FullBlockTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            '-acceptnonstdtxn=1',  # This is a consensus block test, we don't care about tx policy
            '-testactivationheight=bip34@2',
        ]]

    def run_test(self):
        node = self.nodes[0]  # convenience reference to the node

        self.bootstrap_p2p()  # Add one p2p connection to the node

        self.block_heights = {}
        self.coinbase_key = ECKey()
        self.coinbase_key.generate()
        self.coinbase_pubkey = self.coinbase_key.get_pubkey().get_bytes()
        self.tip = None
        self.blocks = {}
        self.genesis_hash = int(self.nodes[0].getbestblockhash(), 16)
        self.block_heights[self.genesis_hash] = 0
        self.spendable_outputs = []

        # Create a new block
        b_dup_cb = self.next_block('dup_cb')
        b_dup_cb.vtx[0].vin[0].scriptSig = DUPLICATE_COINBASE_SCRIPT_SIG
        b_dup_cb.vtx[0].rehash()
        duplicate_tx = b_dup_cb.vtx[0]
        b_dup_cb = self.update_block('dup_cb', [])
        self.send_blocks([b_dup_cb])

        b0 = self.next_block(0)
        self.save_spendable_output()
        self.send_blocks([b0])

        # These constants chosen specifically to trigger an immature coinbase spend
        # at a certain time below.
        NUM_BUFFER_BLOCKS_TO_GENERATE = 99
        NUM_OUTPUTS_TO_COLLECT = 33

        # Allow the block to mature
        blocks = []
        for i in range(NUM_BUFFER_BLOCKS_TO_GENERATE):
            blocks.append(self.next_block(f"maturitybuffer.{i}"))
            self.save_spendable_output()
        self.send_blocks(blocks)

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for _ in range(NUM_OUTPUTS_TO_COLLECT):
            out.append(self.get_spendable_output())

        # Start by building a couple of blocks on top (which output is spent is
        # in parentheses):
        #     genesis -> b1 (0) -> b2 (1)
        b1 = self.next_block(1, spend=out[0])
        self.save_spendable_output()

        b2 = self.next_block(2, spend=out[1])
        self.save_spendable_output()

        self.send_blocks([b1, b2], timeout=4)

        # Select a txn with an output eligible for spending. This won't actually be spent,
        # since we're testing submission of a series of blocks with invalid txns.
        attempt_spend_tx = out[2]

        # Submit blocks for rejection, each of which contains a single transaction
        # (aside from coinbase) which should be considered invalid.
        for TxTemplate in invalid_txs.iter_all_templates():
            template = TxTemplate(spend_tx=attempt_spend_tx)

            if template.valid_in_block:
                continue

            self.log.info(f"Reject block with invalid tx: {TxTemplate.__name__}")
            blockname = f"for_invalid.{TxTemplate.__name__}"
            badblock = self.next_block(blockname)
            badtx = template.get_tx()
            if TxTemplate != invalid_txs.InputMissing:
                self.sign_tx(badtx, attempt_spend_tx)
            badtx.rehash()
            badblock = self.update_block(blockname, [badtx])
            self.send_blocks(
                [badblock], success=False,
                reject_reason=(template.block_reject_reason or template.reject_reason),
                reconnect=True, timeout=2)

            self.move_tip(2)

        # Fork like this:
        #
        #     genesis -> b1 (0) -> b2 (1)
        #                      \-> b3 (1)
        #
        # Nothing should happen at this point. We saw b2 first so it takes priority.
        self.log.info("Don't reorg to a chain of the same length")
        self.move_tip(1)
        b3 = self.next_block(3, spend=out[1])
        txout_b3 = b3.vtx[1]
        self.send_blocks([b3], False)

        # Now we add another block to make the alternative chain longer.
        #
        #     genesis -> b1 (0) -> b2 (1)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reorg to a longer chain")
        b4 = self.next_block(4, spend=out[2])
        self.send_blocks([b4])

        # ... and back to the first chain.
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6 (3)
        #                      \-> b3 (1) -> b4 (2)
        self.move_tip(2)
        b5 = self.next_block(5, spend=out[2])
        self.save_spendable_output()
        self.send_blocks([b5], False)

        self.log.info("Reorg back to the original chain")
        b6 = self.next_block(6, spend=out[3])
        self.send_blocks([b6], True)

        # Try to create a fork that double-spends
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6 (3)
        #                                          \-> b7 (2) -> b8 (4)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a chain with a double spend, even if it is longer")
        self.move_tip(5)
        b7 = self.next_block(7, spend=out[2])
        self.send_blocks([b7], False)

        b8 = self.next_block(8, spend=out[4])
        self.send_blocks([b8], False, reconnect=True)

        # Try to create a block that has too much fee
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6 (3)
        #                                                    \-> b9 (4)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a block where the miner creates too much coinbase reward")
        self.move_tip(6)
        b9 = self.next_block(9, spend=out[4], additional_coinbase_value=1)
        self.send_blocks([b9], success=False, reject_reason='bad-cb-amount', reconnect=True)

        # Create a fork that ends in a block with too much fee (the one that causes the reorg)
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b10 (3) -> b11 (4)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a chain where the miner creates too much coinbase reward, even if the chain is longer")
        self.move_tip(5)
        b10 = self.next_block(10, spend=out[3])
        self.send_blocks([b10], False)

        b11 = self.next_block(11, spend=out[4], additional_coinbase_value=1)
        self.send_blocks([b11], success=False, reject_reason='bad-cb-amount', reconnect=True)

        # Try again, but with a valid fork first
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b14 (5)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a chain where the miner creates too much coinbase reward, even if the chain is longer (on a forked chain)")
        self.move_tip(5)
        b12 = self.next_block(12, spend=out[3])
        self.save_spendable_output()
        b13 = self.next_block(13, spend=out[4])
        self.save_spendable_output()
        b14 = self.next_block(14, spend=out[5], additional_coinbase_value=1)
        self.send_blocks([b12, b13, b14], success=False, reject_reason='bad-cb-amount', reconnect=True)

        # New tip should be b13.
        assert_equal(node.getbestblockhash(), b13.hash)

        # Add a block with MAX_BLOCK_SIGOPS and one with one more sigop
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b15 (5) -> b16 (6)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Accept a block with lots of checksigs")
        lots_of_checksigs = CScript([OP_CHECKSIG] * (MAX_BLOCK_SIGOPS - 1))
        self.move_tip(13)
        b15 = self.next_block(15, spend=out[5], script=lots_of_checksigs)
        self.save_spendable_output()
        self.send_blocks([b15], True)

        self.log.info("Reject a block with too many checksigs")
        too_many_checksigs = CScript([OP_CHECKSIG] * (MAX_BLOCK_SIGOPS))
        b16 = self.next_block(16, spend=out[6], script=too_many_checksigs)
        self.send_blocks([b16], success=False, reject_reason='bad-blk-sigops', reconnect=True)

        # Attempt to spend a transaction created on a different fork
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b15 (5) -> b17 (b3.vtx[1])
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a block with a spend from a re-org'ed out tx")
        self.move_tip(15)
        b17 = self.next_block(17, spend=txout_b3)
        self.send_blocks([b17], success=False, reject_reason='bad-txns-inputs-missingorspent', reconnect=True)

        # Attempt to spend a transaction created on a different fork (on a fork this time)
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b15 (5)
        #                                                                \-> b18 (b3.vtx[1]) -> b19 (6)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a block with a spend from a re-org'ed out tx (on a forked chain)")
        self.move_tip(13)
        b18 = self.next_block(18, spend=txout_b3)
        self.send_blocks([b18], False)

        b19 = self.next_block(19, spend=out[6])
        self.send_blocks([b19], success=False, reject_reason='bad-txns-inputs-missingorspent', reconnect=True)

        # Attempt to spend a coinbase at depth too low
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b15 (5) -> b20 (7)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a block spending an immature coinbase.")
        self.move_tip(15)
        b20 = self.next_block(20, spend=out[7])
        self.send_blocks([b20], success=False, reject_reason='bad-txns-premature-spend-of-coinbase', reconnect=True)

        # Attempt to spend a coinbase at depth too low (on a fork this time)
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b15 (5)
        #                                                                \-> b21 (6) -> b22 (5)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a block spending an immature coinbase (on a forked chain)")
        self.move_tip(13)
        b21 = self.next_block(21, spend=out[6])
        self.send_blocks([b21], False)

        b22 = self.next_block(22, spend=out[5])
        self.send_blocks([b22], success=False, reject_reason='bad-txns-premature-spend-of-coinbase', reconnect=True)

        # Create a block on either side of MAX_BLOCK_WEIGHT and make sure its accepted/rejected
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b15 (5) -> b23 (6)
        #                                                                           \-> b24 (6) -> b25 (7)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Accept a block of weight MAX_BLOCK_WEIGHT")
        self.move_tip(15)
        b23 = self.next_block(23, spend=out[6])
        tx = CTransaction()
        script_length = (MAX_BLOCK_WEIGHT - b23.get_weight() - 276) // 4
        script_output = CScript([b'\x00' * script_length])
        tx.vout.append(CTxOut(0, script_output))
        tx.vin.append(CTxIn(COutPoint(b23.vtx[1].sha256, 0)))
        b23 = self.update_block(23, [tx])
        # Make sure the math above worked out to produce a max-weighted block
        assert_equal(b23.get_weight(), MAX_BLOCK_WEIGHT)
        self.send_blocks([b23], True)
        self.save_spendable_output()

        self.log.info("Reject a block of weight MAX_BLOCK_WEIGHT + 4")
        self.move_tip(15)
        b24 = self.next_block(24, spend=out[6])
        script_length = (MAX_BLOCK_WEIGHT - b24.get_weight() - 276) // 4
        script_output = CScript([b'\x00' * (script_length + 1)])
        tx.vout = [CTxOut(0, script_output)]
        b24 = self.update_block(24, [tx])
        assert_equal(b24.get_weight(), MAX_BLOCK_WEIGHT + 1 * 4)
        self.send_blocks([b24], success=False, reject_reason='bad-blk-length', reconnect=True)

        b25 = self.next_block(25, spend=out[7])
        self.send_blocks([b25], False)

        # Create blocks with a coinbase input script size out of range
        #     genesis -> b1 (0) -> b2 (1) -> b5 (2) -> b6  (3)
        #                                          \-> b12 (3) -> b13 (4) -> b15 (5) -> b23 (6) -> b30 (7)
        #                                                                           \-> ... (6) -> ... (7)
        #                      \-> b3 (1) -> b4 (2)
        self.log.info("Reject a block with coinbase input script size out of range")
        self.move_tip(15)
        b26 = self.next_block(26, spend=out[6])
        b26.vtx[0].vin[0].scriptSig = b'\x00'
        b26.vtx[0].rehash()
        # update_block causes the merkle root to get updated, even with no new
        # transactions, and updates the required state.
        b26 = self.update_block(26, [])
        self.send_blocks([b26], success=False, reject_reason='bad-cb-length', reconnect=True)

        # Extend the b26 chain to make sure bitcoind isn't accepting b26
        b27 = self.next_block(27, spend=out[7])
        self.send_blocks([b27], False)

        # Now try a too-large-coinbase script
        self.move_tip(15)
        b28 = self.next_block(28, spend=out[6])
        b28.vtx[0].vin[0].scriptSig = b'\x00' * 101
        b28.vtx[0].rehash()
        b28 = self.update_block(28, [])
        self.send_blocks([b28], success=False, reject_reason='bad-cb-length', reconnect=True)

        # Extend the b28 chain to make sure bitcoind isn't accepting b28
        b29 = self.next_block(29, spend=out[7])
        self.send_blocks([b29], False)

        # b30 has a max-sized coinbase scriptSig.
        self.move_tip(23)
        b30 = self.next_block(30)
        b30.vtx[0].vin[0].scriptSig = bytes(b30.vtx[0].vin[0].scriptSig)  # Convert CScript to raw bytes
        b30.vtx[0].vin[0].scriptSig += b'\x00' * (100 - len(b30.vtx[0].vin[0].scriptSig))  # Fill with 0s
        assert_equal(len(b30.vtx[0].vin[0].scriptSig), 100)
        b30.vtx[0].rehash()
        b30 = self.update_block(30, [])
        self.send_blocks([b30], True)
        self.save_spendable_output()

        # b31 - b35 - check sigops of OP_CHECKMULTISIG / OP_CHECKMULTISIGVERIFY / OP_CHECKSIGVERIFY
        #
        #     genesis -> ... -> b30 (7) -> b31 (8) -> b33 (9) -> b35 (10)
        #                                                                \-> b36 (11)
        #                                                    \-> b34 (10)
        #                                         \-> b32 (9)
        #

        # MULTISIG: each op code counts as 20 sigops.  To create the edge case, pack another 19 sigops at the end.
        self.log.info("Accept a block with the max number of OP_CHECKMULTISIG sigops")
        lots_of_multisigs = CScript([OP_CHECKMULTISIG] * ((MAX_BLOCK_SIGOPS - 1) // 20) + [OP_CHECKSIG] * 19)
        b31 = self.next_block(31, spend=out[8], script=lots_of_multisigs)
        assert_equal(get_legacy_sigopcount_block(b31), MAX_BLOCK_SIGOPS)
        self.send_blocks([b31], True)
        self.save_spendable_output()

        # this goes over the limit because the coinbase has one sigop
        self.log.info("Reject a block with too many OP_CHECKMULTISIG sigops")
        too_many_multisigs = CScript([OP_CHECKMULTISIG] * (MAX_BLOCK_SIGOPS // 20))
        b32 = self.next_block(32, spend=out[9], script=too_many_multisigs)
        assert_equal(get_legacy_sigopcount_block(b32), MAX_BLOCK_SIGOPS + 1)
        self.send_blocks([b32], success=False, reject_reason='bad-blk-sigops', reconnect=True)

        # CHECKMULTISIGVERIFY
        self.log.info("Accept a block with the max number of OP_CHECKMULTISIGVERIFY sigops")
        self.move_tip(31)
        lots_of_multisigs = CScript([OP_CHECKMULTISIGVERIFY] * ((MAX_BLOCK_SIGOPS - 1) // 20) + [OP_CHECKSIG] * 19)
        b33 = self.next_block(33, spend=out[9], script=lots_of_multisigs)
        self.send_blocks([b33], True)
        self.save_spendable_output()

        self.log.info("Reject a block with too many OP_CHECKMULTISIGVERIFY sigops")
        too_many_multisigs = CScript([OP_CHECKMULTISIGVERIFY] * (MAX_BLOCK_SIGOPS // 20))
        b34 = self.next_block(34, spend=out[10], script=too_many_multisigs)
        self.send_blocks([b34], success=False, reject_reason='bad-blk-sigops', reconnect=True)

        # CHECKSIGVERIFY
        self.log.info("Accept a block with the max number of OP_CHECKSIGVERIFY sigops")
        self.move_tip(33)
        lots_of_checksigs = CScript([OP_CHECKSIGVERIFY] * (MAX_BLOCK_SIGOPS - 1))
        b35 = self.next_block(35, spend=out[10], script=lots_of_checksigs)
        self.send_blocks([b35], True)
        self.save_spendable_output()

        self.log.info("Reject a block with too many OP_CHECKSIGVERIFY sigops")
        too_many_checksigs = CScript([OP_CHECKSIGVERIFY] * (MAX_BLOCK_SIGOPS))
        b36 = self.next_block(36, spend=out[11], script=too_many_checksigs)
        self.send_blocks([b36], success=False, reject_reason='bad-blk-sigops', reconnect=True)

        # Check spending of a transaction in a block which failed to connect
        #
        # b6  (3)
        # b12 (3) -> b13 (4) -> b15 (5) -> b23 (6) -> b30 (7) -> b31 (8) -> b33 (9) -> b35 (10)
        #                                                                                     \-> b37 (11)
        #                                                                                     \-> b38 (11/37)
        #

        # save 37's spendable output, but then double-spend out11 to invalidate the block
        self.log.info("Reject a block spending transaction from a block which failed to connect")
        self.move_tip(35)
        b37 = self.next_block(37, spend=out[11])
        txout_b37 = b37.vtx[1]
        tx = self.create_and_sign_transaction(out[11], 0)
        b37 = self.update_block(37, [tx])
        self.send_blocks([b37], success=False, reject_reason='bad-txns-inputs-missingorspent', reconnect=True)

        # attempt to spend b37's first non-coinbase tx, at which point b37 was still considered valid
        self.move_tip(35)
        b38 = self.next_block(38, spend=txout_b37)
        self.send_blocks([b38], success=False, reject_reason='bad-txns-inputs-missingorspent', reconnect=True)

        # Check P2SH SigOp counting
        #
        #
        #   13 (4) -> b15 (5) -> b23 (6) -> b30 (7) -> b31 (8) -> b33 (9) -> b35 (10) -> b39 (11) -> b41 (12)
        #                                                                                        \-> b40 (12)
        #
        # b39 - create some P2SH outputs that will require 6 sigops to spend:
        #
        #           redeem_script = COINBASE_PUBKEY, (OP_2DUP+OP_CHECKSIGVERIFY) * 5, OP_CHECKSIG
        #           p2sh_script = OP_HASH160, ripemd160(sha256(script)), OP_EQUAL
        #
        self.log.info("Check P2SH SIGOPS are correctly counted")
        self.move_tip(35)
        b39 = self.next_block(39)
        b39_outputs = 0
        b39_sigops_per_output = 6

        # Build the redeem script, hash it, use hash to create the p2sh script
        redeem_script = CScript([self.coinbase_pubkey] + [OP_2DUP, OP_CHECKSIGVERIFY] * 5 + [OP_CHECKSIG])
        p2sh_script = script_to_p2sh_script(redeem_script)

        # Create a transaction that spends one satoshi to the p2sh_script, the rest to OP_TRUE
        # This must be signed because it is spending a coinbase
        spend = out[11]
        tx = self.create_tx(spend, 0, 1, p2sh_script)
        tx.vout.append(CTxOut(spend.vout[0].nValue - 1, CScript([OP_TRUE])))
        self.sign_tx(tx, spend)
        tx.rehash()
        b39 = self.update_block(39, [tx])
        b39_outputs += 1

        # Until block is full, add tx's with 1 satoshi to p2sh_script, the rest to OP_TRUE
        tx_new = None
        tx_last = tx
        total_weight = b39.get_weight()
        while total_weight < MAX_BLOCK_WEIGHT:
            tx_new = self.create_tx(tx_last, 1, 1, p2sh_script)
            tx_new.vout.append(CTxOut(tx_last.vout[1].nValue - 1, CScript([OP_TRUE])))
            tx_new.rehash()
            total_weight += tx_new.get_weight()
            if total_weight >= MAX_BLOCK_WEIGHT:
                break
            b39.vtx.append(tx_new)  # add tx to block
            tx_last = tx_new
            b39_outputs += 1

        # The accounting in the loop above can be off, because it misses the
        # compact size encoding of the number of transactions in the block.
        # Make sure we didn't accidentally make too big a block. Note that the
        # size of the block has non-determinism due to the ECDSA signature in
        # the first transaction.
        while b39.get_weight() >= MAX_BLOCK_WEIGHT:
            del b39.vtx[-1]

        b39 = self.update_block(39, [])
        self.send_blocks([b39], True)
        self.save_spendable_output()

        # Test sigops in P2SH redeem scripts
        #
        # b40 creates 3333 tx's spending the 6-sigop P2SH outputs from b39 for a total of 19998 sigops.
        # The first tx has one sigop and then at the end we add 2 more to put us just over the max.
        #
        # b41 does the same, less one, so it has the maximum sigops permitted.
        #
        self.log.info("Reject a block with too many P2SH sigops")
        self.move_tip(39)
        b40 = self.next_block(40, spend=out[12])
        sigops = get_legacy_sigopcount_block(b40)
        numTxes = (MAX_BLOCK_SIGOPS - sigops) // b39_sigops_per_output
        assert_equal(numTxes <= b39_outputs, True)

        lastOutpoint = COutPoint(b40.vtx[1].sha256, 0)
        new_txs = []
        for i in range(1, numTxes + 1):
            tx = CTransaction()
            tx.vout.append(CTxOut(1, CScript([OP_TRUE])))
            tx.vin.append(CTxIn(lastOutpoint, b''))
            # second input is corresponding P2SH output from b39
            tx.vin.append(CTxIn(COutPoint(b39.vtx[i].sha256, 0), b''))
            # Note: must pass the redeem_script (not p2sh_script) to the signature hash function
            (sighash, err) = LegacySignatureHash(redeem_script, tx, 1, SIGHASH_ALL)
            sig = self.coinbase_key.sign_ecdsa(sighash) + bytes(bytearray([SIGHASH_ALL]))
            scriptSig = CScript([sig, redeem_script])

            tx.vin[1].scriptSig = scriptSig
            tx.rehash()
            new_txs.append(tx)
            lastOutpoint = COutPoint(tx.sha256, 0)

        b40_sigops_to_fill = MAX_BLOCK_SIGOPS - (numTxes * b39_sigops_per_output + sigops) + 1
        tx = CTransaction()
        tx.vin.append(CTxIn(lastOutpoint, b''))
        tx.vout.append(CTxOut(1, CScript([OP_CHECKSIG] * b40_sigops_to_fill)))
        tx.rehash()
        new_txs.append(tx)
        self.update_block(40, new_txs)
        self.send_blocks([b40], success=False, reject_reason='bad-blk-sigops', reconnect=True)

        # same as b40, but one less sigop
        self.log.info("Accept a block with the max number of P2SH sigops")
        self.move_tip(39)
        b41 = self.next_block(41, spend=None)
        self.update_block(41, b40.vtx[1:-1])
        b41_sigops_to_fill = b40_sigops_to_fill - 1
        tx = CTransaction()
        tx.vin.append(CTxIn(lastOutpoint, b''))
        tx.vout.append(CTxOut(1, CScript([OP_CHECKSIG] * b41_sigops_to_fill)))
        tx.rehash()
        self.update_block(41, [tx])
        self.send_blocks([b41], True)

        # Fork off of b39 to create a constant base again
        #
        # b23 (6) -> b30 (7) -> b31 (8) -> b33 (9) -> b35 (10) -> b39 (11) -> b42 (12) -> b43 (13)
        #                                                                  \-> b41 (12)
        #
        self.move_tip(39)
        b42 = self.next_block(42, spend=out[12])
        self.save_spendable_output()

        b43 = self.next_block(43, spend=out[13])
        self.save_spendable_output()
        self.send_blocks([b42, b43], True)

        # Test a number of really invalid scenarios
        #
        #  -> b31 (8) -> b33 (9) -> b35 (10) -> b39 (11) -> b42 (12) -> b43 (13) -> b44 (14)
        #                                                                                   \-> ??? (15)

        # The next few blocks are going to be created "by hand" since they'll do funky things, such as having
        # the first transaction be non-coinbase, etc.  The purpose of b44 is to make sure this works.
        self.log.info("Build block 44 manually")
        height = self.block_heights[self.tip.sha256] + 1
        coinbase = create_coinbase(height, self.coinbase_pubkey)
        b44 = CBlock()
        b44.nTime = self.tip.nTime + 1
        b44.hashPrevBlock = self.tip.sha256
        b44.nBits = 0x207fffff
        b44.vtx.append(coinbase)
        tx = self.create_and_sign_transaction(out[14], 1)
        b44.vtx.append(tx)
        b44.hashMerkleRoot = b44.calc_merkle_root()
        b44.solve()
        self.tip = b44
        self.block_heights[b44.sha256] = height
        self.blocks[44] = b44
        self.send_blocks([b44], True)

        self.log.info("Reject a block with a non-coinbase as the first tx")
        non_coinbase = self.create_tx(out[15], 0, 1)
        b45 = CBlock()
        b45.nTime = self.tip.nTime + 1
        b45.hashPrevBlock = self.tip.sha256
        b45.nBits = 0x207fffff
        b45.vtx.append(non_coinbase)
        b45.hashMerkleRoot = b45.calc_merkle_root()
        b45.solve()
        self.block_heights[b45.sha256] = self.block_heights[self.tip.sha256] + 1
        self.tip = b45
        self.blocks[45] = b45
        self.send_blocks([b45], success=False, reject_reason='bad-cb-missing', reconnect=True)

        self.log.info("Reject a block with no transactions")
        self.move_tip(44)
        b46 = CBlock()
        b46.nTime = b44.nTime + 1
        b46.hashPrevBlock = b44.sha256
        b46.nBits = 0x207fffff
        b46.vtx = []
        b46.hashMerkleRoot = 0
        b46.solve()
        self.block_heights[b46.sha256] = self.block_heights[b44.sha256] + 1
        self.tip = b46
        assert 46 not in self.blocks
        self.blocks[46] = b46
        self.send_blocks([b46], success=False, reject_reason='bad-blk-length', reconnect=True)

        self.log.info("Reject a block with invalid work")
        self.move_tip(44)
        b47 = self.next_block(47)
        target = uint256_from_compact(b47.nBits)
        while b47.sha256 <= target:
            # Rehash nonces until an invalid too-high-hash block is found.
            b47.nNonce += 1
            b47.rehash()
        self.send_blocks([b47], False, force_send=True, reject_reason='high-hash', reconnect=True)

        self.log.info("Reject a block with a timestamp >2 hours in the future")
        self.move_tip(44)
        b48 = self.next_block(48)
        b48.nTime = int(time.time()) + 60 * 60 * 3
        # Header timestamp has changed. Re-solve the block.
        b48.solve()
        self.send_blocks([b48], False, force_send=True, reject_reason='time-too-new')

        self.log.info("Reject a block with invalid merkle hash")
        self.move_tip(44)
        b49 = self.next_block(49)
        b49.hashMerkleRoot += 1
        b49.solve()
        self.send_blocks([b49], success=False, reject_reason='bad-txnmrklroot', reconnect=True)

        self.log.info("Reject a block with incorrect POW limit")
        self.move_tip(44)
        b50 = self.next_block(50)
        b50.nBits = b50.nBits - 1
        b50.solve()
        self.send_blocks([b50], False, force_send=True, reject_reason='bad-diffbits', reconnect=True)

        self.log.info("Reject a block with two coinbase transactions")
        self.move_tip(44)
        b51 = self.next_block(51)
        cb2 = create_coinbase(51, self.coinbase_pubkey)
        b51 = self.update_block(51, [cb2])
        self.send_blocks([b51], success=False, reject_reason='bad-cb-multiple', reconnect=True)

        self.log.info("Reject a block with duplicate transactions")
        # Note: txns have to be in the right position in the merkle tree to trigger this error
        self.move_tip(44)
        b52 = self.next_block(52, spend=out[15])
        tx = self.create_tx(b52.vtx[1], 0, 1)
        b52 = self.update_block(52, [tx, tx])
        self.send_blocks([b52], success=False, reject_reason='bad-txns-duplicate', reconnect=True)
