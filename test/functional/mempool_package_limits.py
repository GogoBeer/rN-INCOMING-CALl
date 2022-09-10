#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test logic for limiting mempool and package ancestors/descendants."""

from decimal import Decimal

from test_framework.address import ADDRESS_BCRT1_P2WSH_OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import (
    COIN,
    CTransaction,
    CTxInWitness,
    tx_from_hex,
    WITNESS_SCALE_FACTOR,
)
from test_framework.script import (
    CScript,
    OP_TRUE,
)
from test_framework.util import (
    assert_equal,
)
from test_framework.wallet import (
    bulk_transaction,
    create_child_with_parents,
    make_chain,
    DEFAULT_FEE,
)

class MempoolPackageLimitsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        self.log.info("Generate blocks to create UTXOs")
        node = self.nodes[0]
        self.privkeys = [node.get_deterministic_priv_key().key]
        self.address = node.get_deterministic_priv_key().address
        self.coins = []
        # The last 100 coinbase transactions are premature
        for b in self.generatetoaddress(node, 200, self.address)[:100]:
            coinbase = node.getblock(blockhash=b, verbosity=2)["tx"][0]
            self.coins.append({
                "txid": coinbase["txid"],
                "amount": coinbase["vout"][0]["value"],
                "scriptPubKey": coinbase["vout"][0]["scriptPubKey"],
            })

        self.test_chain_limits()
        self.test_desc_count_limits()
        self.test_desc_count_limits_2()
        self.test_anc_count_limits()
        self.test_anc_count_limits_2()
        self.test_anc_count_limits_bushy()

        # The node will accept our (nonstandard) extra large OP_RETURN outputs
        self.restart_node(0, extra_args=["-acceptnonstdtxn=1"])
        self.test_anc_size_limits()
        self.test_desc_size_limits()

    def test_chain_limits_helper(self, mempool_count, package_count):
        node = self.nodes[0]
        assert_equal(0, node.getmempoolinfo()["size"])
        first_coin = self.coins.pop()
        spk = None
        txid = first_coin["txid"]
        chain_hex = []
        chain_txns = []
        value = first_coin["amount"]

        for i in range(mempool_count + package_count):
            (tx, txhex, value, spk) = make_chain(node, self.address, self.privkeys, txid, value, 0, spk)
            txid = tx.rehash()
            if i < mempool_count:
                node.sendrawtransaction(txhex)
                assert_equal(node.getmempoolentry(txid)["ancestorcount"], i + 1)
            else:
                chain_hex.append(txhex)
                chain_txns.append(tx)
        testres_too_long = node.testmempoolaccept(rawtxs=chain_hex)
        for txres in testres_too_long:
            assert_equal(txres["package-error"], "package-mempool-limits")

        # Clear mempool and check that the package passes now
        self.generate(node, 1)
        assert all([res["allowed"] for res in node.testmempoolaccept(rawtxs=chain_hex)])

    def test_chain_limits(self):
        """Create chains from mempool and package transactions that are longer than 25,
        but only if both in-mempool and in-package transactions are considered together.
        This checks that both mempool and in-package transactions are taken into account when
        calculating ancestors/descendant limits.
        """
        self.log.info("Check that in-package ancestors count for mempool ancestor limits")

        # 24 transactions in the mempool and 2 in the package. The parent in the package has
        # 24 in-mempool ancestors and 1 in-package descendant. The child has 0 direct parents
        # in the mempool, but 25 in-mempool and in-package ancestors in total.
        self.test_chain_limits_helper(24, 2)
        # 2 transactions in the mempool and 24 in the package.
        self.test_chain_limits_helper(2, 24)
        # 13 transactions in the mempool and 13 in the package.
        self.test_chain_limits_helper(13, 13)

    def test_desc_count_limits(self):
        """Create an 'A' shaped package with 24 transactions in the mempool and 2 in the package:
                    M1
                   ^  ^
                 M2a  M2b
                .       .
               .         .
              .           .
             M12a          ^
            ^              M13b
           ^                 ^
          Pa                  Pb
        The top ancestor in the package exceeds descendant limits but only if the in-mempool and in-package
        descendants are all considered together (24 including in-mempool descendants and 26 including both
        package transactions).
        """
        node = self.nodes[0]
        assert_equal(0, node.getmempoolinfo()["size"])
        self.log.info("Check that in-mempool and in-package descendants are calculated properly in packages")
        # Top parent in mempool, M1
        first_coin = self.coins.pop()
        parent_value = (first_coin["amount"] - Decimal("0.0002")) / 2 # Deduct reasonable fee and make 2 outputs
        inputs = [{"txid": first_coin["txid"], "vout": 0}]
        outputs = [{self.address : parent_value}, {ADDRESS_BCRT1_P2WSH_OP_TRUE : parent_value}]
        rawtx = node.createrawtransaction(inputs, outputs)

        parent_signed = node.signrawtransactionwithkey(hexstring=rawtx, privkeys=self.privkeys)
        assert parent_signed["complete"]
        parent_tx = tx_from_hex(parent_signed["hex"])
        parent_txid = parent_tx.rehash()
        node.sendrawtransaction(parent_signed["hex"])

        package_hex = []

        # Chain A
        spk = parent_tx.vout[0].scriptPubKey.hex()
        value = parent_value
        txid = parent_txid
        for i in range(12):
            (tx, txhex, value, spk) = make_chain(node, self.address, self.privkeys, txid, value, 0, spk)
            txid = tx.rehash()
            if i < 11: # M2a... M12a
                node.sendrawtransaction(txhex)
            else: # Pa
                package_hex.append(txhex)

        # Chain B
        value = parent_value - Decimal("0.0001")
        rawtx_b = node.createrawtransaction([{"txid": parent_txid, "vout": 1}], {self.address : value})
        tx_child_b = tx_from_hex(rawtx_b) # M2b
        tx_child_b.wit.vtxinwit = [CTxInWitness()]
        tx_child_b.wit.vtxinwit[0].scriptWitness.stack = [CScript([OP_TRUE])]
        tx_child_b_hex = tx_child_b.serialize().hex()
        node.sendrawtransaction(tx_child_b_hex)
        spk = tx_child_b.vout[0].scriptPubKey.hex()
        txid = tx_child_b.rehash()
        for i in range(12):
            (tx, txhex, value, spk) = make_chain(node, self.address, self.privkeys, txid, value, 0, spk)
            txid = tx.rehash()
            if i < 11: # M3b... M13b
                node.sendrawtransaction(txhex)
            else: # Pb
                package_hex.append(txhex)

        assert_equal(24, node.getmempoolinfo()["size"])
        assert_equal(2, len(package_hex))
        testres_too_long = node.testmempoolaccept(rawtxs=package_hex)
        for txres in testres_too_long:
            assert_equal(txres["package-error"], "package-mempool-limits")

        # Clear mempool and check that the package passes now
        self.generate(node, 1)
        assert all([res["allowed"] for res in node.testmempoolaccept(rawtxs=package_hex)])

    def test_desc_count_limits_2(self):
        """Create a Package with 24 transaction in mempool and 2 transaction in package:
                      M1
                     ^  ^
                   M2    ^
                   .      ^
                  .        ^
                 .          ^
                M24          ^
                              ^
                              P1
                              ^
                              P2
        P1 has M1 as a mempool ancestor, P2 has no in-mempool ancestors, but when
        combined P2 has M1 as an ancestor and M1 exceeds descendant_limits(23 in-mempool
        descendants + 2 in-package descendants, a total of 26 including itself).
        """

        node = self.nodes[0]
        package_hex = []
        # M1
        first_coin_a = self.coins.pop()
        parent_value = (first_coin_a["amount"] - DEFAULT_FEE) / 2 # Deduct reasonable fee and make 2 outputs
        inputs = [{"txid": first_coin_a["txid"], "vout": 0}]
        outputs = [{self.address : parent_value}, {ADDRESS_BCRT1_P2WSH_OP_TRUE : parent_value}]
        rawtx = node.createrawtransaction(inputs, outputs)

        parent_signed = node.signrawtransactionwithkey(hexstring=rawtx, privkeys=self.privkeys)
        assert parent_signed["complete"]
        parent_tx = tx_from_hex(parent_signed["hex"])
        parent_txid = parent_tx.rehash()
        node.sendrawtransaction(parent_signed["hex"])

        # Chain M2...M24
        spk = parent_tx.vout[0].scriptPubKey.hex()
        value = parent_value
        txid = parent_txid
        for i in range(23): # M2...M24
            (tx, txhex, value, spk) = make_chain(node, self.address, self.privkeys, txid, value, 0, spk)
            txid = tx.rehash()
            node.sendrawtransaction(txhex)

        # P1
        value_p1 = (parent_value - DEFAULT_FEE)
        rawtx_p1 = node.createrawtransaction([{"txid": parent_txid, "vout": 1}], [{self.address : value_p1}])
        tx_child_p1 = tx_from_hex(rawtx_p1)
        tx_child_p1.wit.vtxinwit = [CTxInWitness()]
        tx_child_p1.wit.vtxinwit[0].scriptWitness.stack = [CScript([OP_TRUE])]
        tx_child_p1_hex = tx_child_p1.serialize().hex()
        txid_child_p1 = tx_child_p1.rehash()
        package_hex.append(tx_child_p1_hex)
        tx_child_p1_spk = tx_child_p1.vout[0].scriptPubKey.hex()

        # P2
        (_, tx_child_p2_hex, _, _) = make_chain(node, self.address, self.privkeys, txid_child_p1, value_p1, 0, tx_child_p1_spk)
        package_hex.append(tx_child_p2_hex)

        assert_equal(24, node.getmempoolinfo()["size"])
        assert_equal(2, len(package_hex))
        testres = node.testmempoolaccept(rawtxs=package_hex)
        assert_equal(len(testres), len(package_hex))
        for txres in testres:
            assert_equal(txres["package-error"], "package-mempool-limits")

        # Clear mempool and check that the package passes now
        self.generate(node, 1)
        assert all([res["allowed"] for res in node.testmempoolaccept(rawtxs=package_hex)])

    def test_anc_count_limits(self):
        """Create a 'V' shaped chain with 24 transactions in the mempool and 3 in the package:
        M1a                    M1b
         ^                     ^
          M2a                M2b
           .                 .
            .               .
             .             .
             M12a        M12b
               ^         ^
                Pa     Pb
                 ^    ^
                   Pc
        The lowest descendant, Pc, exceeds ancestor limits, but only if the in-mempool
        and in-package ancestors are all considered together.
        """
        node = self.nodes[0]
        assert_equal(0, node.getmempoolinfo()["size"])
        package_hex = []
        parents_tx = []
        values = []
        scripts = []

        self.log.info("Check that in-mempool and in-package ancestors are calculated properly in packages")

        # Two chains of 13 transactions each
        for _ in range(2):
            spk = None
            top_coin = self.coins.pop()
            txid = top_coin["txid"]
            value = top_coin["amount"]
            for i in range(13):
                (tx, txhex, value, spk) = make_chain(node, self.address, self.privkeys, txid, value, 0, spk)
                txid = tx.rehash()
                if i < 12:
                    node.sendrawtransaction(txhex)
                else: # Save the 13th transaction for the package
                    package_hex.append(txhex)
                    parents_tx.append(tx)
                    scripts.append(spk)
                    values.append(value)

        # Child Pc
        child_hex = create_child_with_parents(node, self.address, self.privkeys, parents_tx, values, scripts)
        package_hex.append(child_hex)

        assert_equal(24, node.getmempoolinfo()["size"])
        assert_equal(3, len(package_hex))
        testres_too_long = node.testmempoolaccept(rawtxs=package_hex)
        for txres in testres_too_long:
            assert_equal(txres["package-error"], "package-mempool-limits")

        # Clear mempool and check that the package passes now
        self.generate(node, 1)
        assert all([res["allowed"] for res in node.testmempoolaccept(rawtxs=package_hex)])

    def test_anc_count_limits_2(self):
        """Create a 'Y' shaped chain with 24 transactions in the mempool and 2 in the package:
        M1a                M1b
         ^                ^
          M2a            M2b
           .            .
            .          .
             .        .
            M12a    M12b
               ^    ^
                 Pc
                 ^
                 Pd
        The lowest descendant, Pd, exceeds ancestor limits, but only if the in-mempool
        and in-package ancestors are all considered together.
        """
        node = self.nodes[0]
        assert_equal(0, node.getmempoolinfo()["size"])
        parents_tx = []
        values = []
        scripts = []

        self.log.info("Check that in-mempool and in-package ancestors are calculated properly in packages")
        # Two chains of 12 transactions each
        for _ in range(2):
            spk = None
            top_coin = self.coins.pop()
            txid = top_coin["txid"]
            value = top_coin["amount"]
            for i in range(12):
                (tx, txhex, value, spk) = make_chain(node, self.address, self.privkeys, txid, value, 0, spk)
                txid = tx.rehash()
                value -= Decimal("0.0001")
                node.sendrawtransaction(txhex)
                if i == 11:
                    # last 2 transactions will be the parents of Pc
                    parents_tx.append(tx)
                    values.append(value)
                    scripts.append(spk)

        # Child Pc
        pc_hex = create_child_with_parents(node, self.address, self.privkeys, parents_tx, values, scripts)
        pc_tx = tx_from_hex(pc_hex)
        pc_value = sum(values) - Decimal("0.0002")
        pc_spk = pc_tx.vout[0].scriptPubKey.hex()

        # Child Pd
        (_, pd_h