// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTIL_HASH_TYPE_H
#define BITCOIN_UTIL_HASH_TYPE_H

template <typename HashType>
class BaseHash
{
protected:
    HashType m_hash