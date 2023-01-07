#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test wallet group functionality."""

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import (
    tx_from_hex,
)
from test_framework.util import (
    assert_approx,
    assert_equal,
)


class WalletGroupTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain =