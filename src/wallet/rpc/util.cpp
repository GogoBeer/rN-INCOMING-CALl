// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/rpc/util.h>

#include <rpc/util.h>
#include <util/translation.h>
#include <util/url.h>
#include <wallet/context.h>
#include <wallet/wallet.h>

#include <univalue.h>

static const s