#!/usr/bin/env python3
# Copyright (c) 2019-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that we reject low difficulty headers to prevent our block tree from filling up with useless bloat"""

from test_framework.messages import (
    CBlockHeader,
    from_hex,
)
from test_framework.p2p import (
    P2PInterface,
    msg_headers,
)
from test_framework.test_framework import BitcoinTestFramework

import os


class RejectLowDifficultyHeadersTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.chain = 'testnet3'  # Use testnet chain because it has an early checkpoint
        self.num_nodes = 2

    def add_options(self, parser):
        parser.add_argument(
            '--datafile',
            default='data/blockheader_testnet3.hex',
            help='Test data file (default: %(default)s)',
        )

    def run_test(self):
        self.log.info("Read headers data")
        self.headers_file_path = os.path.join(os.path.dirname(os.path.realpath(__file__)), self.options.datafile)
        with open(self.headers_file_path, encoding='utf-8') as headers_data:
            h_lines = [l.strip() for l in headers_data.readlines()]

        # The headers data is taken from testnet3 for early blocks from genesis until the first checkpoint. There are
        # two headers with valid POW at height 1 and 2, forking off from genesis. They are indicated by the FORK_PREFIX.
        FORK_PREFIX = 'fork:'
        self.headers = [l for l in h_lines if not l.startswith(FORK_PREFIX)]
        self.headers_fork = [l[len(FORK_PREFIX):] for l in h_lines if l.startswith(FORK_PREFIX)]

        self.headers = [from_hex(CBlockHeader(), h) for h in self.headers]
        self.headers_fork = [from_hex(CBlockHeader(), h) for h in self.headers_fork]

        self.log.info("Feed all non-fork headers, including and up to the first checkpoint")
        peer_checkpoint = self.nodes[0].add_p2p_connection(P2PInterface())
        peer_checkpoint.send_and_ping(msg_headers(self.headers))
        assert {
            'height': 546,
            'hash': '000000002a936ca763904c3c35fce2f3556c559c0214345d31b1bcebf76acb70',
            'branchlen': 546,
            'status': 'headers-only',
        } in self.nodes[0].getchaintips()

        self.log.info("Feed a