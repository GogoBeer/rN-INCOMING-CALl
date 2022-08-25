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
        self.log.debug(f'{http_method} {rest_uri} {body}')
        if http_method == 'GET':
            conn.request('GET', rest_uri)
        elif http_method == 'POST':
            conn.request('POST', rest_uri, body)
        resp = conn.getresponse()

        assert_equal(resp.status, status)

        if ret_type == RetType.OBJ:
            return resp
        elif ret_type == RetType.BYTES:
            return resp.read()
        elif ret_type == RetType.JSON:
            return json.loads(resp.read().decode('utf-8'), parse_float=Decimal)

    def run_test(self):
        self.url = urllib.parse.urlparse(self.nodes[0].url)
        self.log.info("Mine blocks and send Bitcoin to node 1")

        # Random address so node1's balance doesn't increase
        not_related_address = "2MxqoHEdNQTyYeX1mHcbrrpzgojbosTpCvJ"

        self.generate(self.nodes[0], 1)
        self.generatetoaddress(self.nodes[1], 100, not_related_address)

        assert_equal(self.nodes[0].getbalance(), 50)

        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.sync_all()

        self.log.info("Test the /tx URI")

        json_obj = self.test_rest_request(f"/tx/{txid}")
        assert_equal(json_obj['txid'], txid)

        # Check hex format response
        hex_response = self.test_rest_request(f"/tx/{txid}", req_type=ReqType.HEX, ret_type=RetType.OBJ)
        assert_greater_than_or_equal(int(hex_response.getheader('content-length')),
                                     json_obj['size']*2)

        spent = (json_obj['vin'][0]['txid'], json_obj['vin'][0]['vout'])  # get the vin to later check for utxo (should be spent by then)
        # get n of 0.1 outpoint
        n, = filter_output_indices_by_value(json_obj['vout'], Decimal('0.1'))
        spending = (txid, n)

        self.log.info("Query an unspent TXO using the /getutxos URI")

        self.generatetoaddress(self.nodes[1], 1, not_related_address)
        bb_hash = self.nodes[0].getbestblockhash()

        assert_equal(self.nodes[1].getbalance(), Decimal("0.1"))

        # Check chainTip response
        json_obj = self.test_rest_request(f"/getutxos/{spending[0]}-{spending[1]}")
        assert_equal(json_obj['chaintipHash'], bb_hash)

        # Make sure there is one utxo
        assert_equal(len(json_obj['utxos']), 1)
        assert_equal(json_obj['utxos'][0]['value'], Decimal('0.1'))

        self.log.info("Query a spent TXO using the /getutxos URI")

        json_obj = self.test_rest_request(f"/getutxos/{spent[0]}-{spent[1]}")

        # Check chainTip response
        assert_equal(json_obj['chaintipHash'], bb_hash)

        # Make sure there is no utxo in the response because this outpoint has been spent
        assert_equal(len(json_obj['utxos']), 0)

        # Check bitmap
        assert_equal(json_obj['bitmap'], "0")

        self.log.info("Query two TXOs using the /getutxos URI")

        json_obj = self.test_rest_request(f"/getutxos/{spending[0]}-{spending[1]}/{spent[0]}-{spent[1]}")

        assert_equal(len(json_obj['utxos']), 1)
        assert_equal(json_obj['bitmap'], "10")

        self.log.info("Query the TXOs using the /getutxos URI with a binary response")

        bin_request = b'\x01\x02'
        for txid, n in [spending, spent]:
            bin_request += bytes.fromhex(txid)
            bin_request += pack("i", n)

        bin_response = self.test_rest_request("/getutxos", http_method='POST', req_type=ReqType.BIN, body=bin_request, ret_type=RetType.BYTES)
        output = BytesIO(bin_response)
        chain_height, = unpack("<i", output.read(4))
        response_hash = output.read(32)[::-1].hex()

        assert_equal(bb_hash, response_hash)  # check if getutxo's chaintip during calculation was fine
        assert_equal(chain_height, 102)  # chain height must be 102

        self.log.info("Test the /getutxos URI with and without /checkmempool")
        # Create a transaction, check that it's found with /checkmempool, but
        # not found without. Then confirm the transaction and check that it's
        # found with or without /checkmempool.

        # do a tx and don't sync
        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        json_obj = self.test_rest_request(f"/tx/{txid}")
        # get the spent output to later check for utxo (should be spent by then)
        spent = (json_obj['vin'][0]['txid'], json_obj['vin'][0]['vout'])
        # get n of 0.1 outpoint
        n, = filter_output_indices_by_value(json_obj['vout'], Decimal('0.1'))
        spending = (txid, n)

        json_obj = self.test_rest_request(f"/getutxos/{spending[0]}-{spending[1]}")
        assert_equal(len(json_obj['utxos']), 0)

        json_obj = self.test_rest_request(f"/getutxos/checkmempool/{spending[0]}-{spending[1]}")
        assert_equal(len(json_obj['utxos']), 1)

        json_obj = self.test_rest_request(f"/getutxos/{spent[0]}-{spent[1]}")
        assert_equal(len(json_obj['utxos']), 1)

        json_obj = self.test_rest_request(f"/getutxos/checkmempool/{spent[0]}-{spent[1]}")
        assert_equal(len(json_obj['utxos']), 0)

        self.generate(self.nodes[0], 1)

        json_obj = self.test_rest_request(f"/getutxos/{spending[0]}-{spending[1]}")
        assert_equal(len(json_obj['utxos']), 1)

        json_obj = self.test_rest_request(f"/getutxos/checkmempool/{spending[0]}-{spending[1]}")
        assert_equal(len(json_obj['utxos']), 1)

        # Do some invalid requests
        self.test_rest_request("/getutxos", http_method='POST', req_type=ReqType.JSON, body='{"checkmempool', status=400, ret_type=RetType.OBJ)
        self.test_rest_request("/getutxos", http_method='POST', req_type=ReqType.BIN, body='{"checkmempool', status=400, ret_type=RetType.OBJ)
        self.test_rest_request("/getutxos/checkmempool", http_method='POST', req_type=ReqType.JSON, status=400, ret_type=RetType.OBJ)

        # Test limits
        long_uri = '/'.join([f"{txid}-{n_}" for n_ in range(20)])
        self.test_rest_request(f"/getutxos/checkmempool/{long_uri}", http_method='POST', status=400, ret_type=RetType.OBJ)

        long_uri = '/'.join([f'{txid}-{n_}' for n_ in range(15)])
        self.test_rest_request(f"/getutxos/checkmempool/{long_uri}", http_method='POST', status=200)

        self.generate(self.nodes[0], 1)  # generate block to not affect upcoming tests

        self.log.info("Test the /block, /blockhashbyheight and /headers URIs")
        bb_hash = self.nodes[0].getbestblockhash()

        # Check result if block does not exists
        assert_equal(self.test_rest_request('/headers/1/0000000000000000000000000000000000000000000000000000000000000000'), [])
        self.test_rest_request('/block/0000000000000000000000000000000000000000000000000000000000000000', status=404, ret_type=RetType.OBJ)

        # Check result if block is not in the active chain
        self.nodes[0].invalidateblock(bb_hash)
        assert_equal(self.test_rest_request(f'/headers/1/{bb_hash}'), [])
        self.test_rest_request(f'/block/{bb_hash}')
        self.nodes[0].reconsiderblock(bb_hash)

        # Check binary format
        response = self.test_rest_request(f"/block/{bb_hash}", req_type=ReqType.BIN, ret_type=RetType.OBJ)
        assert_greater_than(int(response.getheader('content-length')), BLOCK_HEADER_SIZE)
        response_bytes = response.read()

        # Compare with block header
        response_header = self.test_rest_request(f"/headers/1/{bb_hash}", req_type=ReqType.BIN, ret_type=RetType.OBJ)
        assert_equal(int(response_header.getheader('content-length')), BLOCK_HEADER_SIZE)
        response_header_bytes = response_header.read()
        assert_equal(response_bytes[:BLOCK_HEADER_SIZE], response_header_bytes)

        # Check block hex format
        response_hex = self.test_rest_request(f"/block/{bb_hash}", req_type=ReqType.HEX, ret_type=RetType.OBJ)
        assert_greater_than(int(response_he