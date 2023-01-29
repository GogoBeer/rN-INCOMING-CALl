#!/usr/bin/env python3
# Copyright (c) 2016-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Wallet commands for signing and verifying messages."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_raises_rpc_error,
)

class SignMessagesWithAddressTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-addresstype=legacy"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        message = 'This is just a test message'

        self.log.info('test signing with an address with wallet')
        address = self.nodes[0].getnewaddress()
        signature = self.nodes[0].signmessage(address, message)
        assert self.nodes[0].verifymessage(address, signature, message)

        self.log.info('test verifying with another address s