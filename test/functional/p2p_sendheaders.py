#!/usr/bin/env python3
# Copyright (c) 2014-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test behavior of headers messages to announce blocks.

Setup:

- Two nodes:
    - node0 is the node-under-test. We create two p2p connections to it. The
      first p2p connection is a control and should only ever receive inv's. The
      second p2p connection tests the headers sending logic.
    - node1 is used to create reorgs.

test_null_locators
==================

Sends two getheaders requests with null locator values. First request's hashstop
value refers to validated block, while second request's hashstop value refers to
a block which hasn't been validated. Verifies only the first request returns
headers.

test_nonnull_locators
=====================

Part 1: No headers announcements before "sendheaders"
a. node mines a block [expect: inv]
   send getdata for the block [expect: block]
b. node mines another block [expect: inv]
   send getheaders and getdata [expect: headers, then block]
c. node mines another block [expect: inv]
   peer mines a block, announces with header [expect: getdata]
d. node mines another block [expect: inv]

Part 2: After "sendheaders", headers announcements should generally work.
a. peer sends sendheaders [expect: no response]
   peer sends getheaders with current tip [expect: no response]
b. node mines a block [expect: tip header]
c. for N in 1, ..., 10:
   * for announce-type in {inv, header}
     - peer mines N blocks, announces with announce-type
       [ expect: getheaders/getdata or getdata, deliver block(s) ]
     - node mines a block [ expect: 1 header ]

Part 3: Headers announcements stop after large reorg and resume after getheaders or inv from peer.
- For response-type in {inv, getheaders}
  * 