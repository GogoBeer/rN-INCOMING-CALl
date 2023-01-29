#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test generation and spending of P2TR addresses."""

import random

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.descriptors import descsum_create
from test_framework.script import (
    CScript,
    OP_1,
    OP_CHECKSIG,
    taproot_construct,
)
from test_framework.segwit_addr import encode_segwit_address

# xprvs/xpubs, and m/* derived x-only pubkeys (created using independent implementation)
KEYS = [
    {
        "xprv": "tprv8ZgxMBicQKsPeNLUGrbv3b7qhUk1LQJZAGMuk9gVuKh9sd4BWGp1eMsehUni6qGb8bjkdwBxCbgNGdh2bYGACK5C5dRTaif9KBKGVnSezxV",
        "xpub": "tpubD6NzVbkrYhZ4XqNGAWGWSzmxGWFwVjVTjZxh2fioKbVYi7Jx8fdbprVWsdW7mHwqjchBVas8TLZG4Xwuz4RKU4iaCqiCvoSkFCzQptqk5Y1",
        "pubs": [
            "83d8ee77a0f3a32a5cea96fd1624d623b836c1e5d1ac2dcde46814b619320c18",
            "a30253b018ea6fca966135bf7dd8026915427f24ccf10d4e03f7870f4128569b",
            "a61e5749f2f3db9dc871d7b187e30bfd3297eea2557e9be99897ea8ff7a29a21",
            "8110cf482f66dc37125e619d73075af932521724ffc7108309e88f361efe8c8a",
        ]
    },
    {
        "xprv": "tprv8ZgxMBicQKsPe98QUPieXy5KFPVjuZNpcC9JY7K7buJEm8nWvJogK4kTda7eLjK9U4PnMNbSjEkpjDJazeBZ4rhYNYD7N6GEdaysj1AYSb5",
        "xpub": "tpubD6NzVbkrYhZ4XcACN3PEwNjRpR1g4tZjBVk5pdMR2B6dbd3HYhdGVZNKofAiFZd9okBserZvv58A6tBX4pE64UpXGNTSesfUW7PpW36HuKz",
        "pubs": [
            "f95886b02a84928c5c15bdca32784993105f73de27fa6ad8c1a60389b999267c",
            "71522134160685eb779857033bfc84c7626f13556154653a51dd42619064e679",
            "48957b4158b2c5c3f4c000f51fd2cf0fd5ff8868ebfb194256f5e9131fc74bd8",
            "086dda8139b3a84944010648d2b674b70447be3ae59322c09a4907bc80be62c1",
        ]
    },
    {
        "xprv": "tprv8ZgxMBicQKsPe3ZJmcj9aJ2EPZJYYCh6Lp3v82p75wspgaXmtDZ2RBtkAtWcGnW2VQDzMHQPBkCKMoYTqh1RfJKjv4PcmWVR7KqTpjsdboN",
        "xpub": "tpubD6NzVbkrYhZ4XWb6fGPjyhgLxapUhXszv7ehQYrQWDgDX4nYWcNcbgWcM2RhYo9s2mbZcfZJ8t5LzYcr24FK79zVybsw5Qj3Rtqug8jpJMy",
        "pubs": [
            "9fa5ffb68821cf559001caa0577eeea4978b29416def328a707b15e91701a2f7",
            "8a104c54cd34acba60c97dd8f1f7abc89ba9587afd88dc928e91aca7b1c50d20",
            "13ba6b252a4eb5ef31d39cb521724cdab19a698323f5c17093f28fb1821d052f",
            "f6c2b4863fd5ba1ba09e3a890caed8b75ffbe013ebab31a06ab87cd6f72506af",
        ]
    },
    {
        "xprv": "tprv8ZgxMBicQKsPdKziibn63Rm6aNzp7dSjDnufZMStXr71Huz7iihCRpbZZZ6Voy5HyuHCWx6foHMipzMzUq4tZrtkZ24DJwz5EeNWdsuwX5h",
        "xpub": "tpubD6NzVbkrYhZ4Wo2WcFSgSqRD9QWkGxddo6WSqsVBx7uQ8QEtM7WncKDRjhFEexK119NigyCsFygA4b7sAPQxqebyFGAZ9XVV1BtcgNzbCRR",
        "pubs": [
            "03a669ea926f381582ec4a000b9472ba8a17347f5fb159eddd4a07036a6718eb",
            "bbf56b14b119bccafb686adec2e3d2a6b51b1626213590c3afa815d1fd36f85d",
            "2994519e31bbc238a07d82f85c9832b831705d2ee4a2dbb477ecec8a3f570fe5",
            "68991b5c139a4c479f8c89d6254d288c533aefc0c5b91fac6c89019c4de64988",
        ]
    },
    {
        "xprv": "tprv8ZgxMBicQKsPen4PGtDwURYnCtVMDejyE8vVwMGhQWfVqB2FBPdekhTacDW4vmsKTsgC1wsncVqXiZdX2YFGAnKoLXYf42M78fQJFzuDYFN",
        "xpub": "tpubD6NzVbkrYhZ4YF6BAXtXsqCtmv1HNyvsoSXHDsJzpnTtffH1onTEwC5SnLzCHPKPebh2i7Gxvi9kJNADcpuSmH8oM3rCYcHVtdXHjpYoKnX",
        "pubs": [
            "aba457d16a8d59151c387f24d1eb887efbe24644c1ee64b261282e7baebdb247",
            "c8558b7caf198e892032d91f1a48ee9bdc25462b83b4d0ac62bb7fb2a0df630e",
            "8a4bcaba0e970685858d133a4d0079c8b55bbc755599e212285691eb779ce3dc",
            "b0d68ada13e0d954b3921b88160d4453e9c151131c2b7c724e08f538a666ceb3",
        ]
    },
    {
        "xprv": "tprv8ZgxMBicQKsPd91vCgRmbzA13wyip2RimYeVEkAyZvsEN5pUSB3T43SEBxPsytkxb42d64W2EiRE9CewpJQkzR8HKHLV8Uhk4dMF5yRPaTv",
        "xpub": "tpubD6NzVbkrYhZ4Wc3i6L6N1Pp7cyVeyMcdLrFGXGDGzCfdCa5F4Zs3EY46N72Ws8QDEUYBVwXfDfda2UKSseSdU1fsBegJBhGCZyxkf28bkQ6",
        "pubs": [
            "9b4d495b74887815a1ff623c055c6eac6b6b2e07d2a016d6526ebac71dd99744",
            "8e971b781b7ce7ab742d80278f2dfe7dd330f3efd6d00047f4a2071f2e7553cb",
            "b811d66739b9f07435ccda907ec5cd225355321c35e0a7c7791232f24cf10632",
            "4cd27a5552c272bc80ba544e9cc6340bb906969f5e7a1510b6cef9592683fbc9",
        ]
    },
    {
        "xprv": "tprv8ZgxMBicQKsPdEhLRxxwzTv2t18j7ruoff