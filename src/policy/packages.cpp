// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <policy/packages.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <util/hasher.h>

#include <numeric>
#include <unordered_set>

bool CheckPackage(const Package& txns, PackageValidationState& state)
{
    const unsigned int package_count = txns.size();

    if (package_count > MAX_PACKAGE_COUNT) {
        return state.Invalid(PackageValidationResult::PCKG_