#!/usr/bin/env python3
# Copyright (c) 2017-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Class for bitcoind node under test"""

import contextlib
import decimal
import errno
from enum import Enum
import http.client
import json
import logging
import os
import re
import subprocess
import tempfile
import time
import urllib.parse
import collections
import shlex
import sys
from pathlib import Path

from .authproxy import JSONRPCException
from .descriptors import descsum_create
from .p2p import P2P_SUBVERSION
from .util import (
    MAX_NODES,
    assert_equal,
    append_config,
    delete_cookie_file,
    get_auth_cookie,
    get_rpc_proxy,
    rpc_url,
    wait_until_helper,
    p2p_port,
    EncodeDecimal,
)

BITCOIND_PROC_WAIT_TIMEOUT = 60


class FailedToStartError(Exception):
    """Raised when a node fails to start correctly."""


class ErrorMatch(Enum):
    FULL_TEXT = 1
    FULL_REGEX = 2
    PARTIAL_REGEX = 3


class TestNode():
    """A class for representing a bitcoind node under test.

