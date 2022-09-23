#!/usr/bin/env python3
# Copyright (c) 2014-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mining RPCs

- getmininginfo
- getblocktemplate proposal mode
- submitblock"""

import copy
from decimal import Decimal

from test_framework.blocktools import (
    create_coinbase,
    get_witness_script,
    NORMAL_GBT_REQUEST_PARAMS,
    TIME_GENESIS_BLOCK,
)
from test_framework.messages import (
    CBlock,
    CBlockHeader,
    BLOCK_HEADER_SIZE,
    ser_uint256,
)
from test_framework.p2p import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet import MiniWallet


VERSIONBITS_TOP_BITS = 0x20000000
VERSIONBITS_DEPLOYMENT_TESTDUMMY_BIT = 28


def assert_template(node, block, expect, rehash=True):
    if rehash:
        block.hashMerkleRoot = block.calc_merkle_root()
    rsp = node.getblocktemplate(template_request={
        'data': block.serialize().hex(),
        'mode': 'proposal',
        'rules': ['segwit'],
    })
    assert_equal(rsp, expect)


class MiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.supports_cli = False

    def mine_chain(self):
        self.log.info('Create some old blocks')
        for t in range(TIME_GENESIS_BLOCK, TIME_GENESIS_BLOCK + 200 * 600, 600):
            self.nodes[0].setmocktime(t)
            self.generate(self.wallet, 1, sync_fun=self.no_op)
        mining_info = self.nodes[0].getmininginfo()
        assert_equal(mining_info['blocks'], 200)
        assert_equal(mining_info['currentblocktx'], 0)
        assert_equal(mining_info['currentblockweight'], 4000)

        self.log.info('test blockversion')
        self.restart_node(0, extra_args=[f'-mocktime={t}', '-blockversion=1337'])
        self.connect_nodes(0, 1)
        assert_equal(1337, self.nodes[0].getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)['version'])
        self.restart_node(0, extra_args=[f'-mocktime={t}'])
        self.connect_nodes(0, 1)
        assert_equal(VERSIONBITS_TOP_BITS + (1 << VERSIONBITS_DEPLOYMENT_TESTDUMMY_BIT), self.nodes[0].getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)['version'])
        self.restart_node(0)
        self.connect_nodes(0, 1)

    def run_test(self):
        node = self.nodes[0]
        self.wallet = MiniWallet(node)
        self.mine_chain()

        def assert_submitblock(block, result_str_1, result_str_2=None):
            block.solve()
            result_str_2 = result_str_2 or 'duplicate-invalid'
            assert_equal(result_str_1, node.submitblock(hexdata=block.serialize().hex()))
            assert_equal(result_str_2, node.submitblock(hexdata=block.serialize().hex()))

        self.log.info('getmininginfo')
        mining_info = node.getmininginfo()
        assert_equal(mining_info['blocks'], 200)
        assert_equal(mining_info['chain'], self.chain)
        assert 'currentblocktx' not in mining_info
        assert 'currentblockweight' not in mining_info
        assert_equal(mining_info['difficulty'], Decimal('4.656542373906925E-10'))
        assert_equal(mining_info['networkhashps'], Decimal('0.003333333333333334'))
        assert_equal(mining_info['pooledtx'], 0)

        self.log.info("getblocktemplate: Test default witness commitment")
        txid = int(self.wallet.send_self_transfer(from_node=node)['wtxid'], 16)
        tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)

        # Check that default_witness_commitment is present.
        assert 'default_witness_commitment' in tmpl
        witness_commitment = tmpl['default_witness_commitment']

  