// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2012-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_UI_INTERFACE_H
#define BITCOIN_NODE_UI_INTERFACE_H

#include <functional>
#include <memory>
#include <string>

class CBlockIndex;
enum class SynchronizationState;
struct bilingual_str;

namespace boost {
n