#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test BIP 37
"""

from test_framework.messages import (
    CInv,
    COIN,
    MAX_BLOOM_FILTER_SIZE,
    MAX_BLOOM_HASH_FUNCS,
    MSG_BLOCK,
    MSG_FILTERED_BLOCK,
    msg_filteradd,
    msg_filterclear,
    msg_filterload,
    msg_getdata,
    msg_mempool,
    msg_version,
)
from test_framework.p2p import (
    P2PInterface,
    P2P_SERVICES,
    P2P_SUBVERSION,
    P2P_VERSION,
    p2p_lock,
)
from test_framework.script import MAX_SCRIPT_ELEMENT_SIZE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.wallet import (
    MiniWallet,
    getnewdestination,
)


class P2PBloomFilter(P2PInterface):
    # This is a P2SH watch-only wallet
    watch_script_pubkey = bytes.fromhex('a914ffffffffffffffffffffffffffffffffffffffff87')
    # The initial filter (n=10, fp=0.000001) with just the above scriptPubKey added
    watch_filter_init = msg_filterload(
        data=
        b'@\x00\x08\x00\x80\x00\x00 \x00\xc0\x00 \x04\x00\x08$\x00\x04\x80\x00\x00 \x00\x00\x00\x00\x80\x00\x00@\x00\x02@ \x00',
        nHashFuncs=19,
        nTweak=0,
        nFlags=1,
    )

    def __init__(self):
        super().__init__()
        self._tx_received = False
        self._merkleblock_received = False

    def on_inv(self, message):
        want = msg_getdata()
        for i in message.inv:
            # inv messages can only contain TX or BLOCK, so translate BLOCK to FILTERED_BLOCK
            if i.type == MSG_BLOCK:
                want.inv.append(CInv(MSG_FILTERED_BLOCK, i.hash))
            else:
                want.inv.append(i)
        if len(want.inv):
            self.send_message(want)

    def on_merkleblock(self, message):
        self._merkleblock_received = True

    def on_tx(self, message):
        self._tx_received = True

    @property
    def tx_received(self):
        with p2p_lock:
            return self._tx_received

    @tx_received.setter
    def tx_received(self, value):
        with p2p_lock:
            self._tx_received = value

    @property
    def merkleblock_received(self):
        with p2p_lock:
            return self._merkleblock_received

    @merkleblock_received.setter
    def merkleblock_received(self, value):
        with p2p_lock:
            self._merkleblock_received = value


class FilterTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [[
            '-peerbloomfilters',
            '-whitelist=noban@127.0.0.1',  # immediate tx relay
        ]]

    def generatetoscriptpubkey(self, scriptpubkey):
        """Helper to generate a single block to the given scriptPubKey."""
        return self.generatetodescriptor(self.nodes[0], 1, f'raw({scriptpubkey.hex()})')[0]

    def test_size_limits(self, filter_peer):
        self.log.info('Check that too large filter is rejected')
        with self.nodes[0].assert_debug_log(['Misbehaving']):
            filter_peer.send_and_ping(msg_filterload(data=b'\xbb'*(MAX_BLOOM_FILTER_SIZE+1)))

        self.log.info('Check that max size filter is accepted')
        with self.nodes[0].assert_debug_log([], unexpected_msgs=['Misbehaving']):
            filter_peer.send_and_ping(msg_filterload(data=b'\xbb'*(MAX_BLOOM_FILTER_SIZE)))
        filter_peer.send_and_ping(msg_filterclear())

        self.log.info('Check that filter with too many hash functions is rejected')
        with self.nodes[0].assert_debug_log(['Misbehaving']):
            filter_peer.send_and_ping(msg_filterload(data=b'\xaa', nHashFuncs=MAX_BLOOM_HASH_FUNCS+1))

        self.log.info('Check that filter with max hash functions is accepted')
        with self.nodes[0].assert_debug_log([], unexpected_msgs=['Misbehaving']):
            filter_peer.send_and_ping(msg_filterload(data=b'\xaa', nHashFuncs=MAX_BLOOM_HASH_FUNCS))
        # Don't send filterclear until next two filteradd checks are done

        self.log.info('Check that max size data element to add to the filter is accepted')
        with self.nodes[0].assert_debug_log([], unexpected_msgs=['Misbehaving']):
            filter_peer.send_and_ping(msg_filteradd(data=b'\xcc'*(MAX_SCRIPT_ELEMENT_SIZE)))

        self.log.info('Check that too large data element to add to the filter is rejected')
        with self.nodes[0].assert_debug_log(['Misbehaving']):
            filter_peer.send_and_ping(msg_filteradd(data=b'\xcc'*(MAX_SCRIPT_ELEMENT_SIZE+1)))

        filter_peer.send_and_ping(msg_filterclear())

    def test_msg_mempool(self):
        self.log.info("Check that a node with bloom filters enabled services p2p mempool messages")
        filter_peer = P2PBloomFilter()

        self.log.debug("Create a tx relevant to the peer before connecting")
        txid, _ = self.wallet.send_to(from_node=self.nodes[0], scriptPubKey=filter_peer.watch_script_pubkey, amount=9 * COIN)

        self.log.debug("Send a mempool msg after connecting and check that the tx is received")
        self.nodes[0].add_p2p_connection(filter_peer)
        filter_peer.send_and_ping(filter_peer.watch_filter_init)
        filter_peer.send_message(msg_mempool())
        filter_peer.wait_for_tx(txid)

    def test_frelay_false(self, filter_peer):
        self.log.info("Check that a node with fRelay set to false does not receive invs until the filter is set")
        filter_peer.tx_received = False
        self.wallet.send_to(from_node=self.nodes[0], scriptPubKey=filter_peer.watch_script_pubkey, amount=9 * COIN)
        # Sync to make sure the reason filter_peer doesn't receive the tx is not p2p delays
        filter_peer.sync_with_ping()
        assert not filter_peer.tx_received

        # Clear the mempool so that this transaction does not impact subsequent tests
        self.generate(self.nodes[0], 1)

    def test_filter(self, filter_peer):
        # Set the bloomfilter using filterload
        filter_peer.send_and_ping(filter_peer.watch_filter_