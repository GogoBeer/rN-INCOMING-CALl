#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Tests that a mempool transaction expires after a given timeout and that its
children are removed as well.

Both the default expiry timeout defined by DEFAULT_MEMPOOL_EXPIRY and a user
definable expiry timeout via the '-mempoolexpiry=<n>' command line argument
(<n> is the timeout in hours) are tested.
"""

from datetime import timedelta

from test_framework