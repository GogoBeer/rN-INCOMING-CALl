#!/usr/bin/env python3
# Copyright (c) 2015-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test decoding scripts via decodescript RPC command."""

import json
import os

from test_framework.messages import (
    sha256,
    tx_from_hex,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)


class DecodeScriptTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def decodescript_script_sig(self):
        signature = '304502207fa7a6d1e0ee81132a269ad84e68d695483745cde8b541e3bf630749894e342a022100c1f7ab20e13e22fb95281a870f3dcf38d782e53023ee313d741ad0cfbc0c509001'
        push_signature = '48' + signature
        public_key = '03b0da749730dc9b4b1f4a14d6902877a92541f5368778853d9c4a0cb7802dcfb2'
        push_public_key = '21' + public_key

        # below are test cases for all of the standard transaction types

        self.log.info("- P2PK")
        # the scriptSig of a public key scriptPubKey simply pushes a signature onto the stack
        rpc_result = self.nodes[0].decodescript(push_signature)
        assert_equal(signature, rpc_result['asm'])

        self.log.info("- P2PKH")
        rpc_result = self.nodes[0].decodescript(push_signature + push_public_key)
        assert_equal(signature + ' ' + public_key, rpc_result['asm'])

        self.log.info("- multisig")
        # this also tests the leading portion of a P2SH multisig scriptSig
        # OP_0 <A sig> <B sig>
        rpc_result = self.nodes[0].decodescript('00' + push_signature + push_signature)
        assert_equal('0 ' + signature + ' ' + signature, rpc_result['asm'])

        self.log.info("- P2SH")
        # an empty P2SH redeemScript is valid and makes for a very simple test case.
        # thus, such a spending scriptSig would just need to pass the outer redeemScript
        # hash test and leave true on the top of the stack.
        rpc_result = self.nodes[0].decodescript('5100')
        assert_equal('1 0', rpc_result['asm'])

        # null data scriptSig - no such thing because null data scripts can not be spent.
        # thus, no test case for that standard transaction type is here.

    def decodescript_script_pub_key(self):
        public_key = '03b0da749730dc9b4b1f4a14d6902877a92541f5368778853d9c4a0cb7802dcfb2'
        push_public_key = '21' + public_key
        public_key_hash = '5dd1d3a048119c27b28293056724d9522f26d945'
        push_public_key_hash = '14' + public_key_hash
        uncompressed_public_key = '04b0da749730dc9b4b1f4a14d6902877a92541f5368778853d9c4a0cb7802dcfb25e01fc8fde47c96c98a4f3a8123e33a38a50cf9025cc8c4494a518f991792bb7'
        push_uncompressed_public_key = '41' + uncompressed_public_key
        p2wsh_p2pk_script_hash = 'd8590cf8ea0674cf3d49fd7ca249b85ef7485dea62c138468bddeb20cd6519f7'

        # below are test cases for all of the standard transaction types

        self.log.info("- P2PK")
        # <pubkey> OP_CHECKSIG
        rpc_result = self.nodes[0].decodescript(push_public_key + 'ac')
        assert_equal(public_key + ' OP_CHECKSIG', rpc_result['asm'])
        assert_equal('pubkey', rpc_result['type'])
        # P2PK is translated to P2WPKH
        assert_equal('0 ' + public_key_hash, rpc_result['segwit']['asm'])

        self.log.info("- P2PKH")
        # OP_DUP OP_HASH160 <PubKeyHash> OP_EQUALVERIFY OP_CHECKSIG
        rpc_result = self.nodes[0].decodescript('76a9' + push_public_key_hash + '88ac')
        assert_equal('pubkeyhash', rpc_result['type'])
        assert_equal('OP_DUP OP_HASH160 ' + public_key_hash + ' OP_EQUALVERIFY OP_CHECKSIG', rpc_result['asm'])
        # P2PKH is translated to P2WPKH
        assert_equal('witness_v0_keyhash', rpc_result['segwit']['type'])
        assert_equal('0 ' + public_key_hash, rpc_result['segwit']['asm'])

        self.log.info("- multisig")
        # <m> <A pubkey> <B pubkey> <C pubkey> <n> OP_CHECKMULTISIG
        # just imagine that the pub keys used below are different.
        # for our pur