#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""RPCs that handle raw transaction packages."""

from decimal import Decimal
import random

from test_framework.address import ADDRESS_BCRT1_P2WSH_OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import (
    BIP125_SEQUENCE_NUMBER,
    COIN,
    CTxInWitness,
    tx_from_hex,
)
from test_framework.script import (
    CScript,
    OP_TRUE,
)
from test_framework.util import (
    assert_equal,
)
from test_framework.wallet import (
    create_child_with_parents,
    create_raw_chain,
    make_chain,
)

class RPCPackagesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def assert_testres_equal(self, package_hex, testres_expected):
        """Shuffle package_hex and assert that the testmempoolaccept result matches testres_expected. This should only
        be used to test packages where the order does not matter. The ordering of transactions in package_hex and
        testres_expected must match.
        """
        shuffled_indeces = list(range(len(package_hex)))
        random.shuffle(shuffled_indeces)
        shuffled_package = [package_hex[i] for i in shuffled_indeces]
        shuffled_testres = [testres_expected[i] for i in shuffled_indeces]
        assert_equal(shuffled_testres, self.nodes[0].testmempoolaccept(shuffled_package))

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

        # Create some transactions that can be reused throughout the test. Never submit these to mempool.
        self.independent_txns_hex = []
        self.independent_txns_testres = []
        for _ in range(3):
            coin = self.coins.pop()
            rawtx = node.createrawtransaction([{"txid": coin["txid"], "vout": 0}],
                {self.address : coin["amount"] - Decimal("0.0001")})
            signedtx = node.signrawtransactionwithkey(hexstring=rawtx, privkeys=self.privkeys)
            assert signedtx["complete"]
            testres = node.testmempoolaccept([signedtx["hex"]])
            assert testres[0]["allowed"]
            self.independent_txns_hex.append(signedtx["hex"])
            # testmempoolaccept returns a list of length one, avoid creating a 2D list
            self.independent_txns_testres.append(testres[0])
        self.independent_txns_testres_blank = [{
            "txid": res["txid"], "wtxid": res["wtxid"]} for res in self.independent_txns_testres]

        self.test_independent()
        self.test_chain()
        self.test_multiple_children()
        self.test_multiple_parents()
        self.test_conflicting()
        self.test_rbf()


    def test_independent(self):
        self.log.info("Test multiple independent transactions in a package")
        node = self.nodes[0]
        # For independent transactions, order doesn't matter.
        self.assert_testres_equal(self.independent_txns_hex, self.independent_txns_testres)

        self.log.info("Test an otherwise valid package with an extra garbage tx appended")
        garbage_tx = node.createrawtransaction([{"txid": "00" * 32, "vout": 5}], {self.address: 1})
        tx = tx_from_hex(garbage_tx)
        # Only the txid and wtxids are returned because validation is incomplete for the independent txns.
        # Package validation is atomic: if the node cannot find a UTXO for any single tx in the package,
        # it terminates immediately to avoid unnecessary, expensive signature verification.
        package_bad = self.independent_txns_hex + [garbage_tx]
        testres_bad = self.independent_txns_testres_blank + [{"txid": tx.rehash(), "wtxid": tx.getwtxid(), "allowed": False, "reject-reason": "missing-inputs"}]
        self.assert_testres_equal(package_bad, testres_bad)

        self.log.info("Check testmempoolaccept tells us when some transactions completed validation successfully")
        coin = self.coins.pop()
        tx_bad_sig_hex = node.createrawtransaction([{"txid": coin["txid"], "vout": 0}],
                                           {self.address : coin["amount"] - Decimal("0.0001")})
        tx_bad_sig = tx_from_hex(tx_bad_sig_hex)
        testres_bad_sig = node.testmempoolaccept(self.independent_txns_hex + [tx_bad_sig_hex])
        # By the time the signature for the last transaction is checked, all the other transactions
        # have been fully validated, which is why the node returns full validation results for all
        # transactions here but empty results in other cases.
        assert_equal(testres_bad_sig, self.independent_txns_testres + [{
            "txid": tx_bad_sig.rehash(),
            "wtxid": tx_bad_sig.getwtxid(), "allowed": False,
            "reject-reason": "mandatory-script-verify-flag-failed (Operation not valid with the current stack size)"
        }])

        self.log.info("Check testmempoolaccept reports txns in packages that exceed max feerate")
        coin = self.coins.pop()
        tx_high_fee_raw = node.createrawtransaction([{"txid": coin["txid"], "vout": 0}],
                                           {self.address : coin["amount"] - Decimal("0.999")})
        tx_high_fee_signed = node.signrawtransactionwithkey(hexstring=tx_high_fee_raw, privkeys=self.privkeys)
        assert tx_high_fee_signed["complete"]
        tx_high_fee = tx_from_hex(tx_high_fee_signed["hex"])
        testres_high_fee = node.testmempoolaccept([tx_high_fee_signed["hex"]])
        assert_equal(testres_high_fee, [
            {"txid": tx_high_fee.rehash(), "wtxid": tx_high_fee.getwtxid(), "allowed": False, "reject-reason": "max-fee-exceeded"}
        ])
        package_high_fee = [tx_high_fee_signed["hex"]] + self.independent_txns_hex
        testres_package_high_fee = node.testmempoolaccept(package_high_fee)
        assert_equal(testres_package_high_fee, testres_high_fee + self.independent_txns_testres_blank)

    def test_chain(self):
        node = self.nodes[0]
        first_coin = self.coins.pop()
        (chain_hex, chain_txns) = create_raw_chain(node, first_coin, self.address, self.privkeys)
        self.log.info("Check that testmempoolaccept requires packages to be sorted by dependency")
        assert_equal(node.testmempoolaccept(rawtxs=chain_hex[::-1]),
                [{"txid": tx.rehash(), "wtxid": tx.getwtxid(), "package-error": "package-not-sorted"} for tx in chain_txns[::-1]])

        self.log.info("Testmempoolaccept a chain of 25 transactions")
        testres_multiple = node.testmempoolaccept(rawtxs=chain_hex)

        testres_single = []
        # Test accept and then submit each one individually, which should be identical to package test accept
        for rawtx in chain_hex:
            testres = node.testmempoolaccept([rawtx])
            testres_single.append(testres[0])
            # Submit the transaction now so its child should have no problem validating
            node.sendrawtransaction(rawtx)
        assert_equal(testres_single, testres_multiple)

        # Clean up by clearing the mempool
        self.generate(node, 1)

    def test_multiple_children(self):
        node = self.nodes[0]

        self.log.info("Testmempoolaccept a package in which a transaction has two children within the package")
        first_coin = self.coins.pop()
        value = (first_coin["amount"] - Decimal("0.0002")) / 2 # Deduct reasonable fee and make 2 outputs
        inputs = [{"txid": first_coin["txid"], "vout": 0}]
        outputs = [{self.address : value}, {ADDRESS_BCRT1_P2WSH_OP_TRUE : value}]
       