#!/usr/bin/env python3
# Copyright (c) 2017-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test bitcoin-cli"""

from decimal import Decimal
import re

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
    assert_raises_process_error,
    assert_raises_rpc_error,
    get_auth_cookie,
)
import time

# The block reward of coinbaseoutput.nValue (50) BTC/block matures after
# COINBASE_MATURITY (100) blocks. Therefore, after mining 101 blocks we expect
# node 0 to have a balance of (BLOCKS - COINBASE_MATURITY) * 50 BTC/block.
BLOCKS = COINBASE_MATURITY + 1
BALANCE = (BLOCKS - 100) * 50

JSON_PARSING_ERROR = 'error: Error parsing JSON: foo'
BLOCKS_VALUE_OF_ZERO = 'error: the first argument (number of blocks to generate, default: 1) must be an integer value greater than zero'
TOO_MANY_ARGS = 'error: too many arguments (maximum 2 for nblocks and maxtries)'
WALLET_NOT_LOADED = 'Requested wallet does not exist or is not loaded'
WALLET_NOT_SPECIFIED = 'Wallet file not specified'


def cli_get_info_string_to_dict(cli_get_info_string):
    """Helper method to convert human-readable -getinfo into a dictionary"""
    cli_get_info = {}
    lines = cli_get_info_string.splitlines()
    line_idx = 0
    ansi_escape = re.compile(r'(\x9B|\x1B\[)[0-?]*[ -\/]*[@-~]')
    while line_idx < len(lines):
        # Remove ansi colour code
        line = ansi_escape.sub('', lines[line_idx])
        if "Balances" in line:
            # When "Balances" appears in a line, all of the following lines contain "balance: wallet" until an empty line
            cli_get_info["Balances"] = {}
            while line_idx < len(lines) and not (lines[line_idx + 1] == ''):
                line_idx += 1
                balance, wallet = lines[line_idx].strip().split(" ")
                # Remove right justification padding
                wallet = wallet.strip()
                if wallet == '""':
                    # Set default wallet("") to empty string
                    wallet = ''
                cli_get_info["Balances"][wallet] = balance.strip()
        elif ": " in line:
            key, value = line.split(": ")
            if key == 'Wallet' and value == '""':
                # Set default wallet("") to empty string
                value = ''
            if key == "Proxies" and value == "n/a":
                # Set N/A to empty string to represent no proxy
                value = ''
            cli_get_info[key.strip()] = value.strip()
        line_idx += 1
    return cli_get_info


class TestBitcoinCli(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        if self.is_specified_wallet_compiled():
            self.requires_wallet = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_cli()

    def run_test(self):
        """Main test logic"""
        self.generate(self.nodes[0], BLOCKS)

        self.log.info("Compare responses from getblockchaininfo RPC and `bitcoin-cli getblockchaininfo`")
        cli_response = self.nodes[0].cli.getblockchaininfo()
        rpc_response = self.nodes[0].getblockchaininfo()
        assert_equal(cli_response, rpc_response)

        user, password = get_auth_cookie(self.nodes[0].datadir, self.chain)

        self.log.info("Test -stdinrpcpass option")
        assert_equal(BLOCKS, self.nodes[0].cli(f'-rpcuser={user}', '-stdinrpcpass', input=password).getblockcount())
        assert_raises_process_error(1, 'Incorrect rpcuser or rpcpassword', self.nodes[0].cli(f'-rpcuser={user}', '-stdinrpcpass', input='foo').echo)

        self.log.info("Test -stdin and -stdinrpcpass")
        assert_equal(['foo', 'bar'], self.nodes[0].cli(f'-rpcuser={user}', '-stdin', '-stdinrpcpass', input=f'{password}\nfoo\nbar').echo())
        assert_raises_process_error(1, 'Incorrect rpcuser or rpcpassword', self.nodes[0].cli(f'-rpcuser={user}', '-stdin', '-stdinrpcpass', input='foo').echo)

        self.log.info("Test connecting to a non-existing server")
        assert_raises_process_error(1, "Could not connect to the server", self.nodes[0].cli('-rpcport=1').echo)

        self.log.info("Test connecting with non-existing RPC cookie file")
        assert_raises_process_error(1, "Could not locate RPC credentials", self.nodes[0].cli('-rpccookiefile=does-not-exist', '-rpcpassword=').echo)

        self.log.info("Test -getinfo with arguments fails")
        assert_raises_process_error(1, "-getinfo takes no arguments", self.nodes[0].cli('-getinfo').hel