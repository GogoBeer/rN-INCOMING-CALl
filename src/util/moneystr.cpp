// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/moneystr.h>

#include <consensus/amount.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <optional>

std::string FormatMoney(const CAmount n)
{
    // Note: not using straight sprintf here because we do NOT want
    // localized number formatting.
    static_assert(COIN > 1);
   