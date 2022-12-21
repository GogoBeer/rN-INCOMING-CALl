#!/usr/bin/env python3
# Copyright (c) 2014-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Helpful routines for regression testing."""

from base64 import b64encode
from decimal import Decimal, ROUND_DOWN
from subprocess import CalledProcessError
import hashlib
import inspect
import json
import logging
import os
import re
import time
import unittest

from . import coverage
from .authproxy import AuthServiceProxy, JSONRPCException
from typing import Callable, Optional

logger = logging.getLogger("TestFramework.utils")

# Assert functions
##################


def assert_approx(v, vexp, vspan=0.00001):
    """Assert that `v` is within `vspan` of `vexp`"""
    if v < vexp - vspan:
        raise AssertionError("%s < [%s..%s]" % (str(v), str(vexp - vspan), str(vexp + vspan)))
    if v > vexp + vspan:
        raise AssertionError("%s > [%s..%s]" % (str(v), str(vexp - vspan), str(vexp + vspan)))


def assert_fee_amount(fee, tx_size, feerate_BTC_kvB):
    """Assert the fee is in range."""
    target_fee = get_fee(tx_size, feerate_BTC_kvB)
    if fee < target_fee:
        raise AssertionError("Fee of %s BTC too low! (Should be %s BTC)" % (str(fee), str(target_fee)))
    # allow the wallet's estimation to be at most 2 bytes off
    high_fee = get_fee(tx_size + 2, feerate_BTC_kvB)
    if fe