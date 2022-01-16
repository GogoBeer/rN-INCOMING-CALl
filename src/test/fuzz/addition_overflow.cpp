// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <util/overflow.h>

#include <cstdint>
#include <string>
#include <vector>

#if defined(__has_builtin)
#if __has_builtin(__builtin_add_overflow)
#define HAVE_BUILTIN_ADD_OVERFLOW
#endif
#elif defined(__GNUC__)
#define HAVE_BUILTIN_ADD_OVERFLOW
#endif

namespace {
template <typename T>
void TestAdditionOverflow(FuzzedDataProvider& fuzzed_data_provider)
{
    const T i = fuzzed_data_provider.ConsumeIntegral<T>();
    const T j = fuzzed_data_provider.ConsumeIntegral<T>();
    const bool is_addition_overflow_custom = AdditionOverflow(i, j);
#if defined(HAVE_BUILTIN_ADD_OVERFLO