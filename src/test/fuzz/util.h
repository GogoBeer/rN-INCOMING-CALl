
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_FUZZ_UTIL_H
#define BITCOIN_TEST_FUZZ_UTIL_H

#include <arith_uint256.h>
#include <attributes.h>
#include <chainparamsbase.h>
#include <coins.h>
#include <compat.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <merkleblock.h>
#include <net.h>
#include <netaddress.h>
#include <netbase.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <serialize.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/net.h>
#include <txmempool.h>
#include <uint256.h>
#include <version.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

class PeerManager;

template <typename... Callables>
size_t CallOneOf(FuzzedDataProvider& fuzzed_data_provider, Callables... callables)
{
    constexpr size_t call_size{sizeof...(callables)};
    static_assert(call_size >= 1);
    const size_t call_index{fuzzed_data_provider.ConsumeIntegralInRange<size_t>(0, call_size - 1)};

    size_t i{0};
    ((i++ == call_index ? callables() : void()), ...);
    return call_size;
}

template <typename Collection>
auto& PickValue(FuzzedDataProvider& fuzzed_data_provider, Collection& col)
{
    const auto sz = col.size();
    assert(sz >= 1);
    auto it = col.begin();
    std::advance(it, fuzzed_data_provider.ConsumeIntegralInRange<decltype(sz)>(0, sz - 1));
    return *it;
}

[[nodiscard]] inline std::vector<uint8_t> ConsumeRandomLengthByteVector(FuzzedDataProvider& fuzzed_data_provider, const std::optional<size_t>& max_length = std::nullopt) noexcept
{
    const std::string s = max_length ?
                              fuzzed_data_provider.ConsumeRandomLengthString(*max_length) :
                              fuzzed_data_provider.ConsumeRandomLengthString();
    return {s.begin(), s.end()};
}

[[nodiscard]] inline std::vector<bool> ConsumeRandomLengthBitVector(FuzzedDataProvider& fuzzed_data_provider, const std::optional<size_t>& max_length = std::nullopt) noexcept
{
    return BytesToBits(ConsumeRandomLengthByteVector(fuzzed_data_provider, max_length));
}

[[nodiscard]] inline CDataStream ConsumeDataStream(FuzzedDataProvider& fuzzed_data_provider, const std::optional<size_t>& max_length = std::nullopt) noexcept
{
    return CDataStream{ConsumeRandomLengthByteVector(fuzzed_data_provider, max_length), SER_NETWORK, INIT_PROTO_VERSION};
}

[[nodiscard]] inline std::vector<std::string> ConsumeRandomLengthStringVector(FuzzedDataProvider& fuzzed_data_provider, const size_t max_vector_size = 16, const size_t max_string_length = 16) noexcept
{
    const size_t n_elements = fuzzed_data_provider.ConsumeIntegralInRange<size_t>(0, max_vector_size);
    std::vector<std::string> r;
    for (size_t i = 0; i < n_elements; ++i) {
        r.push_back(fuzzed_data_provider.ConsumeRandomLengthString(max_string_length));
    }
    return r;
}

template <typename T>
[[nodiscard]] inline std::vector<T> ConsumeRandomLengthIntegralVector(FuzzedDataProvider& fuzzed_data_provider, const size_t max_vector_size = 16) noexcept
{
    const size_t n_elements = fuzzed_data_provider.ConsumeIntegralInRange<size_t>(0, max_vector_size);
    std::vector<T> r;
    for (size_t i = 0; i < n_elements; ++i) {
        r.push_back(fuzzed_data_provider.ConsumeIntegral<T>());
    }
    return r;
}

template <typename T>
[[nodiscard]] inline std::optional<T> ConsumeDeserializable(FuzzedDataProvider& fuzzed_data_provider, const std::optional<size_t>& max_length = std::nullopt) noexcept
{
    const std::vector<uint8_t> buffer = ConsumeRandomLengthByteVector(fuzzed_data_provider, max_length);
    CDataStream ds{buffer, SER_NETWORK, INIT_PROTO_VERSION};
    T obj;
    try {
        ds >> obj;
    } catch (const std::ios_base::failure&) {
        return std::nullopt;
    }
    return obj;
}

template <typename WeakEnumType, size_t size>
[[nodiscard]] WeakEnumType ConsumeWeakEnum(FuzzedDataProvider& fuzzed_data_provider, const WeakEnumType (&all_types)[size]) noexcept
{
    return fuzzed_data_provider.ConsumeBool() ?
               fuzzed_data_provider.PickValueInArray<WeakEnumType>(all_types) :
               WeakEnumType(fuzzed_data_provider.ConsumeIntegral<typename std::underlying_type<WeakEnumType>::type>());
}

[[nodiscard]] inline opcodetype ConsumeOpcodeType(FuzzedDataProvider& fuzzed_data_provider) noexcept
{
    return static_cast<opcodetype>(fuzzed_data_provider.ConsumeIntegralInRange<uint32_t>(0, MAX_OPCODE));
}

[[nodiscard]] CAmount ConsumeMoney(FuzzedDataProvider& fuzzed_data_provider, const std::optional<CAmount>& max = std::nullopt) noexcept;

[[nodiscard]] int64_t ConsumeTime(FuzzedDataProvider& fuzzed_data_provider, const std::optional<int64_t>& min = std::nullopt, const std::optional<int64_t>& max = std::nullopt) noexcept;

[[nodiscard]] CMutableTransaction ConsumeTransaction(FuzzedDataProvider& fuzzed_data_provider, const std::optional<std::vector<uint256>>& prevout_txids, const int max_num_in = 10, const int max_num_out = 10) noexcept;

[[nodiscard]] CScriptWitness ConsumeScriptWitness(FuzzedDataProvider& fuzzed_data_provider, const size_t max_stack_elem_size = 32) noexcept;

[[nodiscard]] CScript ConsumeScript(FuzzedDataProvider& fuzzed_data_provider, const bool maybe_p2wsh = false) noexcept;

[[nodiscard]] uint32_t ConsumeSequence(FuzzedDataProvider& fuzzed_data_provider) noexcept;

[[nodiscard]] inline CScriptNum ConsumeScriptNum(FuzzedDataProvider& fuzzed_data_provider) noexcept
{
    return CScriptNum{fuzzed_data_provider.ConsumeIntegral<int64_t>()};
}

[[nodiscard]] inline uint160 ConsumeUInt160(FuzzedDataProvider& fuzzed_data_provider) noexcept
{
    const std::vector<uint8_t> v160 = fuzzed_data_provider.ConsumeBytes<uint8_t>(160 / 8);
    if (v160.size() != 160 / 8) {
        return {};
    }
    return uint160{v160};
}

[[nodiscard]] inline uint256 ConsumeUInt256(FuzzedDataProvider& fuzzed_data_provider) noexcept
{
    const std::vector<uint8_t> v256 = fuzzed_data_provider.ConsumeBytes<uint8_t>(256 / 8);
    if (v256.size() != 256 / 8) {
        return {};
    }
    return uint256{v256};
}

[[nodiscard]] inline arith_uint256 ConsumeArithUInt256(FuzzedDataProvider& fuzzed_data_provider) noexcept
{
    return UintToArith256(ConsumeUInt256(fuzzed_data_provider));
}

[[nodiscard]] CTxMemPoolEntry ConsumeTxMemPoolEntry(FuzzedDataProvider& fuzzed_data_provider, const CTransaction& tx) noexcept;

[[nodiscard]] CTxDestination ConsumeTxDestination(FuzzedDataProvider& fuzzed_data_provider) noexcept;

template <typename T>
[[nodiscard]] bool MultiplicationOverflow(const T i, const T j) noexcept
{
    static_assert(std::is_integral<T>::value, "Integral required.");
    if (std::numeric_limits<T>::is_signed) {
        if (i > 0) {
            if (j > 0) {
                return i > (std::numeric_limits<T>::max() / j);
            } else {
                return j < (std::numeric_limits<T>::min() / i);
            }
        } else {
            if (j > 0) {
                return i < (std::numeric_limits<T>::min() / j);
            } else {
                return i != 0 && (j < (std::numeric_limits<T>::max() / i));
            }
        }
    } else {
        return j != 0 && i > std::numeric_limits<T>::max() / j;
    }
}

[[nodiscard]] bool ContainsSpentInput(const CTransaction& tx, const CCoinsViewCache& inputs) noexcept;