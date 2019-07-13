#!/usr/bin/env python3
# Copyright (c) 2018-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Verify commits against a trusted keys list."""
import argparse
import hashlib
import logging
import os
import subprocess
import sys
import time

GIT = os.getenv('GIT', 'git')

def tree_sha512sum(commit='HEAD'):
    """Calculate the Tree-sha512 for the commit.

    This is copied from github-merge.py. See https://github.com/bitcoin-core/bitcoin-maintainer-tools."""

    # request metadata for entire tree, recursively
    files = []
    blob_by_name = {}
    for line in subprocess.check_output([GIT, 'ls-tree', '--full-tree', '-r', commit]).splitlines():
        name_sep = line.index(b'\t')
        metadata = line[:name_sep].split()  # perms, 'blob', blobid
        assert metadata[1] == b'blob'
        name = line[name_sep + 1:]
        files.append(name)
        blob_by_name[name] = metadata[2]

    files.sort()
    # open connection to git-cat-file in batch mode to request data for all blobs
    # this is much faster than launching it per file
    p = subprocess.Popen([GIT, 'cat-file', '--batch'], stdout=subprocess.PIPE, stdin=subprocess.PIPE)
    overall = hashlib.sha512()
    for f in files:
        blob = blob_by_name[f]
        # request blob
        p.stdin.write(blob + b'\n')
        p.stdin.flush()
        # read header: blob, "blob", size
        reply = p.stdout.readline().split()
        assert reply[0] == blob and reply[1] == b'blob'
        size = int(reply[2])
        # hash the blob data
        intern = hashlib.sha512()
        ptr = 0
        while ptr < size:
            bs = min(65536, size - ptr)
            piece = p.stdout.read(bs)
            if len(piece) == bs:
                intern.update(piece)
            else:
                raise IOError('Premature EOF reading git cat-file output')
            ptr += bs
        dig = intern.hexdigest()
        assert p.stdout.read(1) == b'\n'  # ignore LF that follows blob data
        # update overall hash with file hash
        overall.update(dig.encode("utf-8"))
        overall.update("  ".encode("utf-8"))
        overall.update(f)
        overall.update("\n".encode("utf-8"))
    p.stdin.close()
    if p.wait():
        raise IOError('Non-zero return value executing git cat-file')
    return overall.hexdigest()

def main():

    # Enable debug logging if running in CI
    if 'CI' in os.environ and os.environ['CI'].lower() == "true":
        logging.getLogger().setLevel(logging.DEBUG)

    # Parse arguments
    parser = argparse.ArgumentParser(usage='%(prog)s [options] [commit id]')
    parser.add_argument('--disable-tree-check', action='store_false', dest='verify_tree', help='disable SHA-512 tree check')
    parser.add_argument('--clean-merge', type=float, dest='clean_merge', default=float('inf'), help='Only check clean merge after <NUMBER> days ago (default: %(default)s)', metavar='NUMBER')
    parser.add_argument('commit', nargs='?', default='HEAD', help='Check clean merge up to commit <commit>')
    args = parser.parse_args()

    # get directory of this program and read data files
    dirname = os.path.dirname(os.path.abspath(__file__))
    print("Using verify-commits data from " + dirname)
    verified_root = open(dirname + "/trusted-git-root", "r", encoding="utf8").read().splitlines()[0]
    verified_sha512_root = open(dirname + "/trusted-sha512-root-commit", "r", encoding="utf8").read().splitlines()[0]
    re