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

    This class contains:

    - state about the node (whether it's running, etc)
    - a Python subprocess.Popen object representing the running process
    - an RPC connection to the node
    - one or more P2P connections to the node


    To make things easier for the test writer, any unrecognised messages will
    be dispatched to the RPC connection."""

    def __init__(self, i, datadir, *, chain, rpchost, timewait, timeout_factor, bitcoind, bitcoin_cli, coverage_dir, cwd, extra_conf=None, extra_args=None, use_cli=False, start_perf=False, use_valgrind=False, version=None, descriptors=False):
        """
        Kwargs:
            start_perf (bool): If True, begin profiling the node with `perf` as soon as
                the node starts.
        """

        self.index = i
        self.p2p_conn_index = 1
        self.datadir = datadir
        self.bitcoinconf = os.path.join(self.datadir, "bitcoin.conf")
        self.stdout_dir = os.path.join(self.datadir, "stdout")
        self.stderr_dir = os.path.join(self.datadir, "stderr")
        self.chain = chain
        self.rpchost = rpchost
        self.rpc_timeout = timewait
        self.binary = bitcoind
        self.coverage_dir = coverage_dir
        self.cwd = cwd
        self.descriptors = descriptors
        if extra_conf is not None:
            append_config(datadir, extra_conf)
        # Most callers will just need to add extra args to the standard list below.
        # For those callers that need more flexibility, they can just set the args property directly.
        # Note that common args are set in the config file (see initialize_datadir)
        self.extra_args = extra_args
        self.version = version
        # Configuration for logging is set as command-line args rather than in the bitcoin.conf file.
        # This means that starting a bitcoind using the temp dir to debug a failed test won't
        # spam debug.log.
        self.args = [
            self.binary,
            "-datadir=" + self.datadir,
            "-logtimemicros",
            "-debug",
            "-debugexclude=libevent",
            "-debugexclude=leveldb",
            "-uacomment=testnode%d" % i,
        ]
        if use_valgrind:
            default_suppressions_file = os.path.join(
                os.path.dirname(os.path.realpath(__file__)),
                "..", "..", "..", "contrib", "valgrind.supp")
            suppressions_file = os.getenv("VALGRIND_SUPPRESSIONS_FILE",
                                          default_suppressions_file)
            self.args = ["valgrind", "--suppressions={}".format(suppressions_file),
                         "--gen-suppressions=all", "--exit-on-first-error=yes",
                         "--error-exitcode=1", "--quiet"] + self.args

        if self.version_is_at_least(190000):
            self.args.append("-logthreadnames")
        if self.version_is_at_least(219900):
            self.args.append("-logsourcelocations")

        self.cli = TestNodeCLI(bitcoin_cli, self.datadir)
        self.use_cli = use_cli
        self.start_perf = start_perf

        self.running = False
        self.process = None
        self.rpc_connected = False
        self.rpc = None
        self.url = None
        self.log = logging.getLogger('TestFramework.node%d' % i)
        self.cleanup_on_exit = True # Whether to kill the node when this object goes away
        # Cache perf subprocesses here by their data output filename.
        self.perf_subprocesses = {}

        self.p2ps = []
        self.timeout_factor = timeout_factor

    AddressKeyPair = collections.namedtuple('AddressKeyPair', ['address', 'key'])
    PRIV_KEYS = [
            # address , privkey
            AddressKeyPair('mjTkW3DjgyZck4KbiRusZsqTgaYTxdSz6z', 'cVpF924EspNh8KjYsfhgY96mmxvT6DgdWiTYMtMjuM74hJaU5psW'),
            AddressKeyPair('msX6jQXvxiNhx3Q62PKeLPrhrqZQdSimTg', 'cUxsWyKyZ9MAQTaAhUQWJmBbSvHMwSmuv59KgxQV7oZQU3PXN3KE'),
            AddressKeyPair('mnonCMyH9TmAsSj3M59DsbH8H63U3RKoFP', 'cTrh7dkEAeJd6b3MRX9bZK8eRmNqVCMH3LSUkE3dSFDyzjU38QxK'),
            AddressKeyPair('mqJupas8Dt2uestQDvV2NH3RU8uZh2dqQR', 'cVuKKa7gbehEQvVq717hYcbE9Dqmq7KEBKqWgWrYBa2CKKrhtRim'),
            AddressKeyPair('msYac7Rvd5ywm6pEmkjyxhbCDKqWsVeYws', 'cQDCBuKcjanpXDpCqacNSjYfxeQj8G6CAtH1Dsk3cXyqLNC4RPuh'),
            AddressKeyPair('n2rnuUnwLgXqf9kk2kjvVm8R5BZK1yxQBi', 'cQakmfPSLSqKHyMFGwAqKHgWUiofJCagVGhiB4KCainaeCSxeyYq'),
            AddressKeyPair('myzuPxRwsf3vvGzEuzPfK9Nf2RfwauwYe6', 'cQMpDLJwA8DBe9NcQbdoSb1BhmFxVjWD5gRyrLZCtpuF9Zi3a9RK'),
            AddressKeyPair('mumwTaMtbxEPUswmLBBN3vM9oGRtGBrys8', 'cSXmRKXVcoouhNNVpcNKFfxsTsToY5pvB9DVsFksF1ENunTzRKsy'),
            AddressKeyPair('mpV7aGShMkJCZgbW7F6iZgrvuPHjZjH9qg', 'cSoXt6tm3pqy43UMabY6eUTmR3eSUYFtB2iNQDGgb3VUnRsQys2k'),
            AddressKeyPair('mq4fBNdckGtvY2mijd9am7DRsbRB4KjUkf', 'cN55daf1HotwBAgAKWVgDcoppmUNDtQSfb7XLutTLeAgVc3u8hik'),
            AddressKeyPair('mpFAHDjX7KregM3rVotdXzQmkbwtbQEnZ6', 'cT7qK7g1wkYEMvKowd2ZrX1E5f6JQ7TM246UfqbCiyF7kZhorpX3'),
            AddressKeyPair('mzRe8QZMfGi58KyWCse2exxEFry2sfF2Y7', 'cPiRWE8KMjTRxH1MWkPerhfoHFn5iHPWVK5aPqjW8NxmdwenFinJ'),
    ]

    def get_deterministic_priv_key(self):
        """Return a deterministic priv key in base58, that only depends on the node's index"""
        assert len(self.PRIV_KEYS) == MAX_NODES
        return self.PRIV_KEYS[self.index]

    def _node_msg(self, msg: str) -> str:
        """Return a modified msg that identifies this node by its index as a debugging aid."""
        return "[node %d] %s" % (self.index, msg)

    def _raise_assertion_error(self, msg: str):
        """Raise an AssertionError with msg modified to identify this node."""
        raise AssertionError(self._node_msg(msg))

    def __del__(self):
        # Ensure that we don't leave any bitcoind processes lying around after
        # the test ends
        if self.process and self.cleanup_on_exit:
            # Should only happen on test failure
            # Avoid using logger, as that may have already been shutdown when
            # this destructor is called.
            print(self._node_msg("Cleaning up leftover process"))
            self.process.kill()

    def __getattr__(self, name):
        """Dispatches any unrecognised messages to the RPC connection or a CLI instance."""
        if self.use_cli:
            return getattr(RPCOverloadWrapper(self.cli, True, self.descriptors), name)
        else:
            assert self.rpc_connected and self.rpc is not None, self._node_msg("Error: no RPC connection")
            return getattr(RPCOverloadWrapper(self.rpc, descriptors=self.descriptors), name)

    def start(self, extra_args=None, *, cwd=None, stdout=None, stderr=None, **kwargs):
        """Start the node."""
        if extra_args is None:
            extra_args = self.extra_args

        # Add a new stdout and stderr file each time bitcoind is started
        if stderr is None:
            stderr = tempfile.NamedTemporaryFile(dir=self.stderr_dir, delete=False)
        if stdout is None:
            stdout = tempfile.NamedTemporaryFile(dir=self.stdout_dir, delete=False)
        self.stderr = stderr
        self.stdout = stdout

        if cwd is None:
            cwd = self.cwd

        # Delete any existing cookie file -- if such a file exists (eg due to
        # unclean shutdown), it will get overwritten anyway by bitcoind, and
        # potentially interfere with our attempt to authenticate
        delete_cookie_file(self.datadir, self.chain)

        # add environment variable LIBC_FATAL_STDERR_=1 so that libc errors are written to stderr and not the terminal
        subp_env = dict(os.environ, LIBC_FATAL_STDERR_="1")

        self.process = subprocess.Popen(self.args + extra_args, env=subp_env, stdout=stdout, stderr=stderr, cwd=cwd, **kwargs)

        self.running = True
        self.log.debug("bitcoind started, waiting for RPC to come up")

        if self.start_perf:
            self._start_perf()

    def wait_for_rpc_connection(self):
        """Sets up an RPC connection to the bitcoind process. Returns False if unable to connect."""
        # Poll at a rate of four times per second
        poll_per_s = 4
        for _ in range(poll_per_s * self.rpc_timeout):
            if self.process.poll() is not None:
                raise FailedToStartError(self._node_msg(
                    'bitcoind exited with status {} during initialization'.format(self.process.returncode)))
            try:
                rpc = get_rpc_proxy(
                    rpc_url(self.datadir, self.index, self.chain, self.rpchost),
                    self.index,
                    timeout=self.rpc_timeout // 2,  # Shorter timeout to allow for one retry in case of ETIMEDOUT
                    coveragedir=self.coverage_dir,
                )
                rpc.getblockcount()
                # If the call to getblockcount() succeeds then the RPC connection is up
                if self.version_is_at_least(190000):
                    # getmempoolinfo.loaded is available since commit
                    # bb8ae2c (version 0.19.0)
                    wait_until_helper(lambda: rpc.getmempoolinfo()['loaded'], timeout_factor=self.timeout_factor)
                    # Wait for the node to finish reindex, block import, and
                    # loading the mempool. Usually importing happens fast or
                    # even "immediate" when the node is started. However, there
                    # is no guarantee and sometimes ThreadImport might finish
                    # later. This is going to cause intermittent test failures,
                    # because generally the tests assume the node is fully
                    # ready after being started.
                    #
                    # For example, the node will reject block messages from p2p
                    # when it is still importing with the error "Unexpected
                    # block message received"
                    #
                    # The wait is done here to make tests as robust as possible
                    # and prevent racy tests and intermittent failures as much
                    # as possible. Some tests might not need this, but the
                    # overhead is trivial, and the added guarantees are worth
                    # the minimal performance cost.
                self.log.debug("RPC successfully started")
                if self.use_cli:
                    return
                self.rpc = rpc
                self.rpc_connected = True
                self.url = self.rpc.rpc_url
                return
            except JSONRPCException as e:  # Initialization phase
                # -28 RPC in warmup
                # -342 Service unavailable, RPC server started but is shutting down due to error
                if e.error['code'] != -28 and e.error['code'] != -342:
                    raise  # unknown JSON RPC exception
            except ConnectionResetError:
                # This might happen when the RPC server is in warmup, but shut down before the call to getblockcount
                # succeeds. Try again to properly raise the FailedToStartError
                pass
            except OSError as e:
                if e.errno == errno.ETIMEDOUT:
                    pass  # Treat identical to ConnectionResetError
                elif e.errno == errno.ECONNREFUSED:
                    pass  # Port not yet open?
                else:
                    raise  # unknown OS error
            except ValueError as e:  # cookie file not found and no rpcuser or rpcpassword; bitcoind is still starting
                if "No RPC credentials" not in str(e):
                    raise
            time.sleep(1.0 / poll_per_s)
        self._raise_assertion_error("Unable to connect to bitcoind after {}s".format(self.rpc_timeout))

    def wait_for_cookie_credentials(self):
        """Ensures auth cookie credentials can be read, e.g. for testing CLI with -rpcwait before RPC connection is up."""
        self.log.debug("Waiting for cookie credentials")
        # Poll at a rate of four times per second.
        poll_per_s = 4
        for _ in range(poll_per_s * self.rpc_timeout):
            try:
                get_auth_cookie(self.datadir, self.chain)
                self.log.debug("Cookie credentials successfully retrieved")
                return
            except ValueError:  # cookie file not found and no rpcuser or rpcpassword; bitcoind is still starting
                pass            # so we continue polling until RPC credentials are retrieved
            time.sleep(1.0 / poll_per_s)
        self._raise_assertion_error("Unable to retrieve cookie credentials after {}s".format(self.rpc_timeout))

    def generate(self, nblocks, maxtries=1000000, **kwargs):
        self.log.debug("TestNode.generate() dispatches `generate` call to `generatetoaddress`")
        return self.generatetoaddress(nblocks=nblocks, address=self.get_deterministic_priv_key().address, maxtries=maxtries, **kwargs)

    def generateblock(self, *args, invalid_call, **kwargs):
        assert not invalid_call
        return self.__getattr__('generateblock')(*args, **kwargs)

    def generatetoaddress(self, *args, invalid_call, **kwargs):
        assert not invalid_call
        return self.__getattr__('generatetoaddress')(*args, **kwargs)

    def generatetodescriptor(self, *args, invalid_call, **kwargs):
        assert not invalid_call
        return self.__getattr__('generatetodescriptor')(*args, **kwargs)

    def get_wallet_rpc(self, wallet_name):
        if self.use_cli:
            return RPCOverloadWrapper(self.cli("-rpcwallet={}".format(wallet_name)), True, self.descriptors)
        else:
            assert self.rpc_connected and self.rpc, self._node_msg("RPC not connected")
            wallet_path = "wallet/{}".format(urllib.parse.quote(wallet_name))
            return RPCOverloadWrapper(self.rpc / wallet_path, descriptors=self.descriptors)

    def version_is_at_least(self, ver):
        return self.version is None or self.version >= ver

    def stop_node(self, expected_stderr='', *, wait=0, wait_until_stopped=True):
        """Stop the node."""
        if not self.running:
            return
        self.log.debug("Stopping node")
        try:
            # Do not use wait argument when testing older nodes, e.g. in feature_backwards_compatibility.py
            if self.version_is_at_least(180000):
                self.stop(wait=wait)
            else:
                self.stop()
        except http.client.CannotSendRequest:
            self.log.exception("Unable to stop node.")

        # If there are any running perf processes, stop them.
        for profile_name in tuple(self.perf_subprocesses.keys()):
            self._stop_perf(profile_name)

        # Check that stderr is as expected
        self.stderr.seek(0)
        stderr = self.stderr.read().decode('utf-8').strip()
        if stderr != expected_stderr:
            raise AssertionError("Unexpected stderr {} != {}".format(stderr, expected_stderr))

        self.stdout.close()
        self.stderr.close()

        del self.p2ps[:]

        if wait_until_stopped:
            self.wait_until_stopped()

    def is_node_stopped(self):
        """Checks whether the node has stopped.

        Returns True if the node has stopped. False otherwise.
        This method is responsible for freeing resources (self.process)."""
        if not self.running:
            return True
        return_code = self.process.poll()
        if return_code is None:
            return False

        # process has stopped. Assert that it didn't return an error code.
        assert return_code == 0, self._node_msg(
            "Node returned n