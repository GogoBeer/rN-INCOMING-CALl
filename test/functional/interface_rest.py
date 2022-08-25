#!/usr/bin/env python3
# Copyright (c) 2014-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the REST API."""

from decimal import Decimal
from enum import Enum
from io import BytesIO
import json
from struct import pack, unpack

import http.client
import urllib.parse

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
)

from test_framework.messages import BLOCK_HEADER_SIZE

class ReqType(Enum):
    JSON = 1
    BIN = 2
    HEX = 3

class RetType(Enum):
    OBJ = 1
    BYTES = 2
    JSON = 3

def filter_output_indices_by_value(vouts, value):
    for vout in vouts:
        if vout['value'] == value:
            yield vout['n']

class RESTTest (BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-rest", "-blockfilterindex=1"], []]
        self.supports_cli = False

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def test_rest_request(self, uri, http_method='GET', req_type=ReqType.JSON, body='', status=200, ret_type=RetType.JSON):
        rest_uri = '/rest' + uri
        if req_type == ReqType.JSON:
            rest_uri += '.json'
        elif req_type == ReqType.BIN:
            rest_uri += '.bin'
        elif req_type == ReqType.HEX:
            rest_uri += '.hex'

        conn = http.client.HTTPConnection(self.url.hostname, self.url.port)
        self.lo