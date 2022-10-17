#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test per-peer message capture capability.

Additionally, the output of contrib/message-capture/message-capture-parser.py should be verified manually.
"""

import glob
from io import BytesIO
import os

from test_framework.p2p import P2PDataStore, MESSAGEMAP
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

TIME_SIZE = 8
LENGTH_SIZE = 4
MSGTYPE_SIZE = 12

def mini_parser(dat_file):
    """Parse a data file created by CaptureMessage.

    From the data file we'll only check the structure.

    We won't care about things like:
    - Deserializing the payload of the message
        - This is managed by the deserialize methods in test_framework.messages
    - The order of the messages
        - There's no reason why we can't, say, change the order of the messages in the handshake
    - Message Type
        - We can add new message types

    We're ignoring these because they're simply too brittle to test here.
    """
    with open(dat_file, 'rb') as f_in:
        # This should have at least one message in it
        assert(os.fstat(f_in.fileno()).st_size >= TIME_SIZE + LENGT