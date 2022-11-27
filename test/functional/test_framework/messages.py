#!/usr/bin/env python3
# Copyright (c) 2010 ArtForz -- public domain half-a-node
# Copyright (c) 2012 Jeff Garzik
# Copyright (c) 2010-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Bitcoin test framework primitive and message structures

CBlock, CTransaction, CBlockHeader, CTxIn, CTxOut, etc....:
    data structures that should map to corresponding structures in
    bitcoin/primitives

msg_block, msg_tx, msg_headers, etc.:
    data structures that represent network messages

ser_*, deser_*: functions that handle serialization/deserialization.

Classes use __slots__ to ensure extraneous attributes aren't accidentally added
by tests, compromising their intended effect.
"""
from base64 import b32decode, b32encode
import copy
import hashlib
from io import BytesIO
import math
import random
import socket
import struct
import time

from test_framework.siphash import siphash256
from test_framework.util import assert_equal

MAX_LOCATOR_SZ = 101
MAX_BLOCK_WEIGHT = 4000000
MAX_BLOOM_FILTER_SIZE = 36000
MAX_BLOOM_HASH_FUNCS = 50

COIN = 100000000  # 1 b