#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Script for verifying Bitcoin Core release binaries

This script attempts to download the signature file SHA256SUMS.asc from
bitcoincore.org and bitcoin.org and compares them.
It first checks if the signature passes, and then downloads the files
specified in the file, and checks if the hashes of these files match those
that are specified in the signature file.
The script returns 0 if everything passes the checks. It returns 1 if either
the signature check or the hash check doesn't pass. If an error occurs the
return value is >= 2.
"""
from hashlib import sha256
import os
import subprocess
import sys
from textwrap import indent

WORKINGDIR = "/tmp/bitcoin_verify_binaries"
HASHFILE = "hashes.tmp"
HOST1 = "https://bitcoincore.org"
HOST2 = "https://bitcoin.org"
VERSIONPREFIX = "bitcoin-core-"
SIGNATUREFILENAME = "SHA256SUMS.asc"


def parse_version_string(version_str):
    if version_str.startswith(VERSIONPREFIX):  # remove version prefix
        version_str = version_str[len(VERSIONPREFIX):]

    parts = version_str.split('-')
    version_base = parts[0]
    version_rc = ""
    version_os = ""
    if len(parts) == 2:  # "<version>-rcN" or "version-platform"
        if "rc" in parts[1]:
            version_rc = parts[1]
        else:
            version_os = parts[1]
    elif len(parts) == 3:  # "<version>-rcN-platform"
        version_rc = parts[1]
        version_os = parts[2]

    return version_base, version_rc, version_os


def download_with_wget(remote_file, local_file=None):
    if local_file:
        wget_args = ['wget', '-O', local_file, remote_file]
    el