#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check that it's not possible to start a second bitcoind instance using the same datadir or wallet."""
import os
import random
import string

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch

class FilelockTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def setup_network(self):
        self.add_nodes(self.num_nodes, extra_args=None)
        self.nodes[0].start()
        self.nodes[0].wait_for_rpc_connec