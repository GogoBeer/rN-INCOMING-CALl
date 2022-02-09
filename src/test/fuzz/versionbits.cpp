// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <primitives/block.h>
#include <util/system.h>
#include <versionbits.h>

#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {
class TestConditionChecker : public AbstractThresholdConditionChecker
{
private:
    mutable ThresholdConditionCache m_cache;
    const Consensus::Params dummy_params{};

public:
    const int64_t m_begin;
    const int64_t m_end;
    const int m_period;
    const int m_threshold;
    const int m_min_activation_height;
    const int m_bit;

    TestConditionChecker(int64_t begin, int64_t end, int period, int threshold, int min_activation_height, int bit)
        : m_begin{begin}, m_end{end}, m_period{period}, m_threshold{threshold}, m_min_activation_height{min_activation_height}, m_bit{bit}
    {
        assert(m_period > 0);
        assert(0 <= m_threshold && m_threshold <= m_period);
        assert(0 <= m_bit && m_bit < 32 && m_bit < VERSIONBITS_NUM_BITS);
        assert(0 <= m_min_activation_height);
    }

    bool Condition(const CBlockIndex* pindex, const Consensus::Params& params) const override { return Condition(pindex->nVersion); }
    int64_t BeginTime(const Consensus::Params& params) const override { return m_begin; }
    int64_t EndTime(const Consensus::Params& params) const override { return m_end; }
    int Period(const Consensus::Params& params) const override { return m_period; }
    int Threshold(const Consensus::Params& params) const override { return m_threshold; }
    int MinActivationHeight(const Consensus::Params& params) const override { return m_min_activation_height; }

    ThresholdState GetStateFor(const CBlockIndex* pindexPrev) const { return AbstractThresholdConditionChecker::GetStateFor(pindexPrev, dummy_params, m_cache); }
    int GetStateSinceHeightFor(const CBlockIndex* pindexPrev) const { return AbstractThresholdConditionChecker::GetStateSinceHeightFor(pindexPrev, dummy_params, m_cache); }
    BIP9Stats GetStateStatisticsFor(const CBlockIndex* pindexPrev) const { return AbstractThresholdConditionChecker::GetStateStatisticsFor(pindexPrev, dummy_params); }

    bool Condition(int32_t version) const
    {
        uint32_t mask = ((uint32_t)1) << m_bit;
        return (((version & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) && (version & mask) != 0);
    }

    bool Condition(const CBlockIndex* pindex) const { return Condition(pindex->nVersion); }
};

/** Track blocks mined for test */
class Blocks
{
private:
    std::vector<std::unique_ptr<CBlockIndex>> m_blocks;
    const uint32_t m_start_time;
    const uint32_t m_interval;
    const int32_t m_signal;
    const int32_t m_no_signal;

public:
    Blocks(uint32_t start_time, uint32_t interval, int32_t si