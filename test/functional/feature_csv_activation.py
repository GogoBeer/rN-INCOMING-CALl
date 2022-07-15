#!/usr/bin/env python3
# Copyright (c) 2015-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test CSV soft fork activation.

This soft fork will activate the following BIPS:
BIP 68  - nSequence relative lock times
BIP 112 - CHECKSEQUENCEVERIFY
BIP 113 - MedianTimePast semantics for nLockTime

mine 83 blocks whose coinbases will be used to generate inputs for our tests
mine 344 blocks and seed block chain with the 83 inputs used for our tests at height 427
mine 2 blocks and verify soft fork not yet activated
mine 1 block and test that soft fork is activated (rules enforced for next block)
Test BIP 113 is enforced
Mine 4 blocks so next height is 580 and test BIP 68 is enforced for time and height
Mine 1 block so next height is 581 and test BIP 68 now passes time but not height
Mine 1 block so next height is 582 and test BIP 68 now passes time and height
Test that BIP 112 is enforced

Various transactions will be used to test that the BIPs rules are not enforced before the soft fork activates
And that after the soft fork activates transactions pass and fail as they should according to the rules.
For each BIP, transactions of versions 1 and 2 will be tested.
----------------
BIP 113:
bip113tx - modify the nLocktime variable

BIP 68:
bip68txs - 16 txs with nSequence relative locktime of 10 with various bits set as per the relative_locktimes below

BIP 112:
bip112txs_vary_nSequence - 16 txs with nSequence relative_locktimes of 10 evaluated against 10 OP_CSV OP_DROP
bip112txs_vary_nSequence_9 - 16 txs with nSequence relative_locktimes of 9 evaluated against 10 OP_CSV OP_DROP
bip112txs_vary_OP_CSV - 16 txs with nSequence = 10 evaluated against varying {relative_locktimes of 10} OP_CSV OP_DROP
bip112txs_vary_OP_CSV_9 - 16 txs with nSequence = 9 evaluated against varying {relative_locktimes of 10} OP_CSV OP_DROP
bip112tx_special - test negative argument to OP_CSV
bip112tx_emptystack - test empty stack (= no argument) OP_CSV
"""
from itertools import product
import time

from test_framework.blocktools import (
    create_block,
    create_coinbase,
)
from test_framework.p2p import P2PDataStore
from test_framework.script import (
    CScript,
    OP_CHECKSEQUENCEVERIFY,
    OP_DROP,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    softfork_active,
)
from test_framework.wallet import (
    MiniWallet,
    MiniWalletMode,
)

TESTING_TX_COUNT = 83  # Number of testing transactions: 1 BIP113 tx, 16 BIP68 txs, 66 BIP112 txs (see comments above)
COINBASE_BLOCK_COUNT = TESTING_TX_COUNT  # Number of coinbase blocks we need to generate as inputs for our txs
BASE_RELATIVE_LOCKTIME = 10
SEQ_DISABLE_FLAG = 1 << 31
SEQ_RANDOM_HIGH_BIT = 1 << 25
SEQ_TYPE_FLAG = 1 << 22
SEQ_RANDOM_LOW_BIT = 1 << 18


def relative_locktime(sdf, srhb, stf, srlb):
    """Returns a locktime with certain bits set."""

    locktime = BASE_RELATIVE_LOCKTIME
    if sdf:
        locktime |= SEQ_DISABLE_FLAG
    if srhb:
        locktime |= SEQ_RANDOM_HIGH_BIT
    if stf:
        locktime |= SEQ_TYPE_FLAG
    if srlb:
        locktime |= SEQ_RANDOM_LOW_BIT
    return locktime


def all_rlt_txs(txs):
    return [tx['tx'] for tx in txs]


CSV_ACTIVATION_HEIGHT = 432


class BIP68_112_113Test(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            '-whitelist=noban@127.0.0.1',
            f'-testactivationheight=csv@{CSV_ACTIVATION_HEIGHT}',
            '-par=1',  # Use only one script thread to get the exact reject reason for testing
        ]]
        self.supports_cli = False

    def create_self_transfer_from_utxo(self, input_tx):
        utxo = self.miniwallet.get_utxo(txid=input_tx.rehash(), mark_as_spent=False)
        tx = self.miniwallet.create_self_transfer(from_node=self.nodes[0], utxo_to_spend=utxo)['tx']
        return tx

    def create_bip112special(self, input, txversion):
        tx = self.create_self_transfer_from_utxo(input)
        tx.nVersion = txversion
        self.miniwallet.sign_tx(tx)
        tx.vin[0].scriptSig = CScript([-1, OP_CHECKSEQUENCEVERIFY, OP_DROP] + list(CScript(tx.vin[0].scriptSig)))
        return tx

    def create_bip112emptystack(self, input, txversion):
        tx = self.create_self_transfer_from_utxo(input)
        tx.nVersion = txversion
        self.miniwallet.sign_tx(tx)
        tx.vin[0].scriptSig = CScript([OP_CHECKSEQUENCEVERIFY] + list(CScript(tx.vin[0].scriptSig)))
        return tx

    def send_generic_input_tx(self, coinbases):
        input_txid = self.nodes[0].getblock(coinbases.pop(), 2)['tx'][0]['txid']
        utxo_to_spend = self.miniwallet.get_utxo(txid=input_txid)
        return self.miniwallet.send_self_transfer(from_node=self.nodes[0], utxo_to_spend=utxo_to_spend)['tx']

    def create_bip68txs(self, bip68inputs, txversion, locktime_delta=0):
        """Returns a list of bip68 transactions with different bits set."""
        txs = []
        assert len(bip68inputs) >= 16
        for i, (sdf, srhb, stf, srlb) in enumerate(product(*[[True, False]] * 4)):
            locktime = relative_locktime(sdf, srhb, stf, srlb)
            tx = self.create_self_transfer_from_utxo(bip68inputs[i])
            tx.nVersion = txversion
            tx.vin[0].nSequence = locktime + locktime_delta
            self.miniwallet.sign_tx(tx)
            tx.rehash()
            txs.append({'tx': tx, 'sdf': sdf, 'stf': stf})

        return txs

    def create_bip112txs(self, bip112inputs, varyOP_CSV, txversion, locktime_delta=0):
        """Returns a list of bip68 transactions with different bits set."""
        txs = []
        assert len(bip112inputs) >= 16
        for i, (sdf, srhb, stf, srlb) in enumerate(product(*[[True, False]] * 4)):
            locktime = relative_locktime(sdf, srhb, stf, srlb)
            tx = self.create_self_transfer_from_utxo(bip112inputs[i])
            if varyOP_CSV:  # if varying OP_CSV, nSequence is fixed
                tx.vin[0].nSequence = BASE_RELATIVE_LOCKTIME + locktime_delta
            else:  # vary nSequence instead, OP_CSV is fixed
                tx.vin[0].nSequence = locktime + locktime_delta
            tx.nVersion = txversion
            self.miniwallet.sign_tx(tx)
        