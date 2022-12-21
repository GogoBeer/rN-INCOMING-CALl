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
    if fee > high_fee:
        raise AssertionError("Fee of %s BTC too high! (Should be %s BTC)" % (str(fee), str(target_fee)))


def assert_equal(thing1, thing2, *args):
    if thing1 != thing2 or any(thing1 != arg for arg in args):
        raise AssertionError("not(%s)" % " == ".join(str(arg) for arg in (thing1, thing2) + args))


def assert_greater_than(thing1, thing2):
    if thing1 <= thing2:
        raise AssertionError("%s <= %s" % (str(thing1), str(thing2)))


def assert_greater_than_or_equal(thing1, thing2):
    if thing1 < thing2:
        raise AssertionError("%s < %s" % (str(thing1), str(thing2)))


def assert_raises(exc, fun, *args, **kwds):
    assert_raises_message(exc, None, fun, *args, **kwds)


def assert_raises_message(exc, message, fun, *args, **kwds):
    try:
        fun(*args, **kwds)
    except JSONRPCException:
        raise AssertionError("Use assert_raises_rpc_error() to test RPC failures")
    except exc as e:
        if message is not None and message not in e.error['message']:
            raise AssertionError(
                "Expected substring not found in error message:\nsubstring: '{}'\nerror message: '{}'.".format(
                    message, e.error['message']))
    except Exception as e:
        raise AssertionError("Unexpected exception raised: " + type(e).__name__)
    else:
        raise AssertionError("No exception raised")


def assert_raises_process_error(returncode: int, output: str, fun: Callable, *args, **kwds):
    """Execute a process and asserts the process return code and output.

    Calls function `fun` with arguments `args` and `kwds`. Catches a CalledProcessError
    and verifies that the return code and output are as expected. Throws AssertionError if
    no CalledProcessError was raised or if the return code and output are not as expected.

    Args:
        returncode: the process return code.
        output: [a substring of] the process output.
        fun: the function to call. This should execute a process.
        args*: positional arguments for the function.
        kwds**: named arguments for the function.
    """
    try:
        fun(*args, **kwds)
    except CalledProcessError as e:
        if returncode != e.returncode:
            raise AssertionError("Unexpected returncode %i" % e.returncode)
        if output not in e.output:
            raise AssertionError("Expected substring not found:" + e.output)
    else:
        raise AssertionError("No exception raised")


def assert_raises_rpc_error(code: Optional[int], message: Optional[str], fun: Callable, *args, **kwds):
    """Run an RPC and verify that a specific JSONRPC exception code and message is raised.

    Calls function `fun` with arguments `args` and `kwds`. Catches a JSONRPCException
    and verifies that the error code and message are as expected. Throws AssertionError if
    no JSONRPCException was raised or if the error code/message are not as expected.

    Args:
        code: the error code returned by the RPC call (defined in src/rpc/protocol.h).
            Set to None if checking the error code is not required.
        message: [a substring of] the error string returned by the RPC call.
            Set to None if checking the error string is not required.
        fun: the function to call. This should be the name of an RPC.
        args*: positional arguments for the function.
        kwds**: named arguments for the function.
    """
    assert try_rpc(code, message, fun, *args, **kwds), "No exception raised"


def try_rpc(code, message, fun, *args, **kwds):
    """Tries to run an rpc command.

    Test against error code and message if the rpc fails.
    Returns whether a JSONRPCException was raised."""
    try:
        fun(*args, **kwds)
    except JSONRPCException as e:
        # JSONRPCException was thrown as expected. Check the code and message values are correct.
        if (code is not None) and (code != e.error["code"]):
            raise AssertionError("Unexpected JSONRPC error code %i" % e.error["code"])
        if (message is not None) and (message not in e.error['message']):
            raise AssertionError(
                "Expected substring not found in error message:\nsubstring: '{}'\nerror message: '{}'.".format(
                    message, e.error['message']))
        return True
    except Exception as e:
        raise AssertionError("Unexpected exception raised: " + type(e).__name__)
    else:
        return False


def assert_is_hex_string(string):
    try:
        int(string, 16)
    except Exception as e:
        raise AssertionError("Couldn't interpret %r as hexadecimal; raised: %s" % (string, e))


def assert_is_hash_string(string, length=64):
    if not isinstance(string, str):
        raise AssertionError("Expected a string, got type %r" % type(string))
    elif length and len(string) != length:
        raise AssertionError("String of length %d expected; got %d" % (length, len(string)))
    elif not re.match('[abcdef0-9]+$', string):
        raise AssertionError("String %r contains invalid characters for a hash." % string)


def assert_array_result(object_array, to_match, expected, should_not_find=False):
    """
        Pass in array of JSON objects, a dictionary with key/value pairs
        to match against, and another dictionary with expected key/value
        pairs.
        If the should_not_find flag is true, to_match should not be found
        in object_array
        """
    if should_not_find:
        assert_equal(expected, {})
    num_matched = 0
    for item in object_array:
        all_match = True
        for key, value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        elif should_not_find:
            num_matched = num_matched + 1
        for key, value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s" % (str(item), str(key), str(value)))
            num_matched = num_matched + 1
    if num_matched == 0 and not should_not_find:
        raise AssertionError("No objects matched %s" % (str(to_match)))
    if num_matched > 0 and should_not_find:
        raise AssertionError("Objects were found %s" % (str(to_match)))


# Utility functions
###################


def check_json_precision():
    """Make sure json library being used does not lose precision converting BTC values"""
    n = Decimal("20000000.00000003")
    satoshis = int(json.loads(json.dumps(float(n))) * 1.0e8)
    if satoshis != 2000000000000003:
        raise RuntimeError("JSON encode/decode loses precision")


def EncodeDecimal(o):
    if isinstance(o, Decimal):
        return str(o)
    raise TypeError(repr(o) + " is not JSON serializable")


def count_bytes(hex_string):
    return len(bytearray.fromhex(hex_string))


def str_to_b64str(string):
    return b64encode(string.encode('utf-8')).decode('ascii')


def ceildiv(a, b):
    """Divide 2 ints and round up to next int rather than round down"""
    return -(-a // b)


def get_fee(tx_size, feerate_btc_kvb):
    """Calculate the fee in BTC given a feerate is BTC/kvB. Reflects CFeeRate::GetFee"""
    feerate_sat_kvb = int(feerate_btc_kvb * Decimal(1e8)) # Fee in sat/kvb as an int to avoid float precision errors
    target_fee_sat = ceildiv(feerate_sat_kvb * tx_size, 1000) # Round calculated fee up to nearest sat
    return satoshi_round(target_fee_sat / Decimal(1e8)) # Truncate BTC result to nearest sat


def satoshi_round(amount):
    return Decimal(amount).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)


def wait_until_helper(predicate, *, attempts=float('inf'), timeout=float('inf'), lock=None, timeout_factor=1.0):
    """Sleep until the predicate resolves to be True.

    Warning: Note that this method is not recommended to be used in tests as it is
    not aware of the context of the test framework. Using the `wait_until()` members
    from `BitcoinTestF