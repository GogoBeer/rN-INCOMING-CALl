#!/usr/bin/env python3
# Copyright (c) 2014-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test fee estimation code."""
from decimal import Decimal
import os
import random

from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
)
from test_framework.script import (
    CScript,
    OP_1,
    OP_DROP,
    OP_TRUE,
)
from test_framework.script_util import (
    script_to_p2sh_script,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
    satoshi_round,
)

# Construct 2 trivial P2SH's and the ScriptSigs that spend them
# So we can create many transactions without needing to spend
# time signing.
SCRIPT = CScript([OP_1, OP_DROP])
P2SH = script_to_p2sh_script(SCRIPT)
REDEEM_SCRIPT = CScript([OP_TRUE, SCRIPT])


def small_txpuzzle_randfee(
    from_node, conflist, unconflist, amount, min_fee, fee_increment
):
    """Create and send a transaction with a random fee.

    The transaction pays to a trivial P2SH script, and assumes that its inputs
    are of the same form.
    The function takes a list of confirmed outputs and unconfirmed outputs
    and attempts to use the confirmed list first for its inputs.
    It adds the newly created outputs to the unconfirmed list.
    Returns (raw transaction, fee)."""

    # It's best to exponentially distribute our random fees
    # because the buckets are exponentially spaced.
    # Exponentially distributed from 1-128 * fee_increment
    rand_fee = float(fee_increment) * (1.1892 ** random.randint(0, 28))
    # Total fee ranges from min_fee to min_fee + 127*fee_increment
    fee = min_fee - fee_increment + satoshi_round(rand_fee)
    tx = CTransaction()
    total_in = Decimal("0.00000000")
    while total_in <= (amount + fee) and len(conflist) > 0:
        t = conflist.pop(0)
        total_in += t["amount"]
        tx.vin.append(CTxIn(COutPoint(int(t["txid"], 16), t["vout"]), REDEEM_SCRIPT))
    while total_in <= (amount + fee) and len(unconflist) > 0:
        t = unconflist.pop(0)
        total_in += t["amount"]
        tx.vin.append(CTxIn(COutPoint(int(t["txid"], 16), t["vout"]), REDEEM_SCRIPT))
    if total_in <= amount + fee:
        raise RuntimeError(f"Insufficient funds: need {amount + fee}, have {total_in}")
    tx.vout.append(CTxOut(int((total_in - amount - fee) * COIN), P2SH))
    tx.vout.append(CTxOut(int(amount * COIN), P2SH))
    txid = from_node.sendrawtransaction(hexstring=tx.serialize().hex(), maxfeerate=0)
    unconflist.append({"txid": txid, "vout": 0, "amount": total_in - amount - fee})
    unconflist.append({"txid": txid, "vout": 1, "amount": amount})

    return (tx.serialize().hex(), fee)


def check_raw_estimates(node, fees_seen):
    """Call estimaterawfee and verify that the estimates meet certain invariants."""

    delta = 1.0e-6  # account for rounding error
    for i in range(1, 26):
        for _, e in node.estimaterawfee(i).items():
            feerate = float(e["feerate"])
            assert_greater_than(feerate, 0)

            if feerate + delta < min(fees_seen) or feerate - delta > max(fees_seen):
                raise AssertionError(
                    f"Estimated fee ({feerate}) out of range ({min(fees_seen)},{max(fees_seen)})"
                )


def check_smart_estimates(node, fees_seen):
    """Call estimatesmartfee and verify that the estimates meet certain invariants."""

    delta = 1.0e-6  # account for rounding error
    last_feerate = float(max(fees_seen))
    all_smart_estimates = [node.estimatesmartfee(i) for i in range(1, 26)]
    mempoolMinFee = node.getmempoolinfo()["mempoolminfee"]
    minRelaytxFee = node.getmempoolinfo()["minrelaytxfee"]
    for i, e in enumerate(all_smart_estimates):  # estimate is for i+1
        feerate = float(e["feerate"])
        assert_greater_than(feerate, 0)
        assert_greater_than_or_equal(feerate, float(mempoolMinFee))
        assert_greater_than_or_equal(feerate, float(minRelaytxFee))

        if feerate + delta < min(fees_seen) or feerate - delta > max(fees_seen):
            raise AssertionError(
                f"Estimated fee ({feerate}) out of range ({min(fees_seen)},{max(fees_seen)})"
            )
        if feerate - delta > last_feerate:
            raise AssertionError(
                f"Estimated fee ({feerate}) larger than last fee ({last_feerate}) for lower number of confirms"
            )
        last_feerate = feerate

        if i == 0:
            assert_equal(e["blocks"], 2)
        else:
            assert_greater_than_or_equal(i + 1, e["blocks"])


def check_estimates(node, fees_seen):
    check_raw_estimates(node, fees_seen)
    check_smart_estimates(node, fees_seen)


def send_tx(node, utxo, feerate):
    """Broadcast a 1in-1out transaction with a specific input and feerate (sat/vb)."""
    tx = CTransaction()
    tx.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), REDEEM_SCRIPT)]
    tx.vout = [CTxOut(int(utxo["amount"] * COIN), P2SH)]

    # vbytes == bytes as we are using legacy transactions
    fee = tx.get_vsize() * feerate
    tx.vout[0].nValue -= fee

    return node.sendrawtransaction(tx.serialize().hex())


class EstimateFeeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # Force fSendTrickle to true (via whitelist.noban)
        self.extra_args = [
            ["-whitelist=noban@127.0.0.1"],
            ["-whitelist=noban@127.0.0.1", "-blockmaxweight=68000"],
            ["-whitelist=noban@127.0.0.1", "-blockmaxweight=32000"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        """
        We'll setup the network to have 3 nodes that all mine with different parameters.
        But first we need to use one node to create a lot of outputs
        which we will use to generate our transactions.
        """
        self.add_nodes(3, extra_args=self.extra_args)
        # Use node0 to mine blocks for input splitting
        # Node1 mines small blocks but that are bigger than the expected transaction rate.
        # NOTE: the CreateNewBlock code starts counting block weight at 4,000 weight,
        # (68k weight is room enough for 120 or so transactions)
        # Node2 is a stingy miner, that
        # produces too small blocks (room for only 55 or so transactions)
        self.start_nodes()
        self.import_deterministic_coinbase_privkeys()
        self.stop_nodes()

    def transact_and_mine(self, numblocks, mining_node):
        min_fee = Decimal("0.00001")
        # We will now mine numblocks blocks generating on average 100 transactions between each block
        # We shuffle our confirmed txout set before each set of transactions
        # small_txpuzzle_randfee will use the transactions that have inputs already in the chain when possible
        # resorting to tx's that depend on the mempool when those run out
        for _ in range(numblocks):
            random.shuffle(self.confutxo)
            for _ in range(random.randrange(100 - 50, 100 + 50)):
                from_index = random.randint(1, 2)
                (txhex, fee) = small_txpuzzle_randfee(
                    self.nodes[from_inde