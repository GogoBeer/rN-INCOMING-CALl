#!/usr/bin/env python3
# Copyright (c) 2019-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the importdescriptors RPC.

Test importdescriptors by generating keys on node0, importing the corresponding
descriptors on node1 and then testing the address info for the different address
variants.

- `get_generate_key()` is called to generate keys and return the privkeys,
  pubkeys and all variants of scriptPubKey and address.
- `test_importdesc()` is called to send an importdescriptors call to node1, test
  success, and (if unsuccessful) test the error code and error message returned.
- `test_address()` is called to call getaddressinfo for an address on node1
  and test the values returned."""

from test_framework.address import key_to_p2pkh
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.descriptors import descsum_create
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    find_vout_for_address,
)
from test_framework.wallet_util import (
    get_generate_key,
    test_address,
)

class ImportDescriptorsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [["-addresstype=legacy"],
                           ["-addresstype=bech32", "-keypool=5"]
                          ]
        self.setup_clean_chain = True
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def test_importdesc(self, req, success, error_code=None, error_message=None, warnings=None, wallet=None):
        """Run importdescriptors and assert success"""
        if warnings is None:
            warnings = []
        wrpc = self.nodes[1].get_wallet_rpc('w1')
        if wallet is not None:
            wrpc = wallet

        result = wrpc.importdescriptors([req])
        observed_warnings = []
        if 'warnings' in result[0]:
            observed_warnings = result[0]['warnings']
        assert_equal("\n".join(sorted(warnings)), "\n".join(sorted(observed_warnings)))
        assert_equal(result[0]['success'], success)
        if error_code is not None:
            assert_equal(result[0]['error']['code'], error_code)
            assert_equal(result[0]['error']['message'], error_message)

    def run_test(self):
        self.log.info('Setting up wallets')
        self.nodes[0].createwallet(wallet_name='w0', disable_private_keys=False, descriptors=True)
        w0 = self.nodes[0].get_wallet_rpc('w0')

        self.nodes[1].createwallet(wallet_name='w1', disable_private_keys=True, blank=True, descriptors=True)
        w1 = self.nodes[1].get_wallet_rpc('w1')
        assert_equal(w1.getwalletinfo()['keypoolsize'], 0)

        self.nodes[1].createwallet(wallet_name="wpriv", disable_private_keys=False, blank=True, descriptors=True)
        wpriv = self.nodes[1].get_wallet_rpc("wpriv")
        assert_equal(wpriv.getwalletinfo()['keypoolsize'], 0)

        self.log.info('Mining coins')
        self.generatetoaddress(self.nodes[0], COINBASE_MATURITY + 1, w0.getnewaddress())

        # RPC importdescriptors -----------------------------------------------

        # # Test import fails if no descriptor present
        self.log.info("Import should fail if a descriptor is not provided")
        self.test_importdesc({"timestamp": "now"},
                             success=False,
                             error_code=-8,
                             error_message='Descriptor not found.')

        # # Test importing of a P2PKH descriptor
        key = get_generate_key()
        self.log.info("Should import a p2pkh descriptor")
        import_request = {"desc": descsum_create("pkh(" + key.pubkey + ")"),
                 "timestamp": "now",
                 "label": "Descriptor import test"}
        self.test_importdesc(import_request, success=True)
        test_address(w1,
                     key.p2pkh_addr,
                     solvable=True,
                     ismine=True,
                     labels=["Descriptor import test"])
        assert_equal(w1.getwalletinfo()['keypoolsize'], 0)

        self.log.info("Test can import same descriptor with public key twice")
        self.test_importdesc(import_request, success=True)

        self.log.info("Test can update descriptor label")
        self.test_importdesc({**import_request, "label": "Updated label"}, success=True)
        test_address(w1, key.p2pkh_addr, solvable=True, ismine=True, labels=["Updated label"])

        self.log.info("Internal addresses cannot have labels")
        self.test_importdesc({**import_request, "internal": True},
                             success=False,
                             error_code=-8,
                             error_message="Internal addresses should not have a label")

        self.log.info("Internal addresses should be detected as s