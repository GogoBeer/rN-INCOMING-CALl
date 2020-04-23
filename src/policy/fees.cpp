// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/fees.h>

#include <clientversion.h>
#include <fs.h>
#include <logging.h>
#include <streams.h>
#include <txmempool.h>
#include <util/serfloat.h>
#include <util/system.h>

static const char* FEE_ESTIMATES_FILENAME = "fee_estimates.dat";

static constexpr double INF_FEERATE = 1e99;

std::string StringForFeeEstimateHorizon(FeeEstimateHorizon horizon)
{
    switch (horizon) {
    case FeeEstimateHorizon::SHORT_HALFLIFE: return "short";
    case FeeEstimateHorizon::MED_HALFLIFE: return "medium";
    case FeeEstimateHorizon::LONG_HALFLIFE: return "long";
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

namespace {

struct EncodedDoubleFormatter
{
    template<typename Stream> void Ser(Stream &s, double v)
    {
        s << EncodeDouble(v);
    }

    template<typename Stream> void Unser(Stream& s, double& v)
    {
        uint64_t encoded;
        s >> encoded;
        v = DecodeDouble(encoded);
    }
};

} // namespace

/**
 * We will instantiate an instance of this class to track transactions that were
 * included in a block. We will lump transactions into a bucket according to their
 * approximate feerate and then track how long it took for those txs to be included in a block
 *
 * The tracking of unconfirmed (mempool) transactions is completely independent of the
 * historical tracking of transactions that have been confirmed in a block.
 */
class TxConfirmStats
{
private:
    //Define the buckets we will group transactions into
    const std::vector<double>& buckets;              // The upper-bound of the range for the bucket (inclusive)
    const std::map<double, unsigned int>& bucketMap; // Map of bucket upper-bound to index into all vectors by bucket

    // For each bucket X:
    // Count the total # of txs in each bucket
    // Track the historical moving average of this total over blocks
    std::vector<double> txCtAvg;

    // Count the total # of txs confirmed within Y blocks in each bucket
    // Track the historical moving average of these totals over blocks
    std::vector<std::vector<double>> confAvg; // confAvg[Y][X]

    // Track moving avg of txs which have been evicted from the mempool
    // after failing to be confirmed within Y blocks
    std::vector<std::vector<double>> failAvg; // failAvg[Y][X]

    // Sum the total feerate of all tx's in each bucket
    // Track the historical moving average of this total over blocks
    std::vector<double> m_feerate_avg;

    // Combine the conf counts with tx counts to calculate the confirmation % for each Y,X
    // Combine the total value with the tx counts to calculate the avg feerate per bucket

    double decay;

    // Resolution (# of blocks) with which confirmations are tracked
    unsigned int scale;

    // Mempool counts of outstanding transactions
    // For each bucket X, track the number of transactions in the mempool
    // that are unconfirmed for each possible confirmation value Y
    std::vector<std::vector<int> > unconfTxs;  //unconfTxs[Y][X]
    // transactions still unconfirmed after GetMaxConfirms for each bucket
    std::vector<int> oldUnconfTxs;

    void resizeInMemoryCounters(size_t newbuckets);

public:
    /**
     * Create new TxConfirmStats. This is called by BlockPolicyEstimator's
     * constructor with default values.
     * @param defaultBuckets contains the upper limits for the bucket boundaries
     * @param maxPeriods max number of periods to track
     * @param decay how much to decay the historical moving average per block
     */
    TxConfirmStats(const std::vector<double>& defaultBuckets, const std::map<double, unsigned int>& defaultBucketMap,
                   unsigned int maxPeriods, double decay, unsigned int scale);

    /** Roll the circular buffer for unconfirmed txs*/
    void ClearCurrent(unsigned int nBlockHeight);

    /**
     * Record a new transaction data point in the current block stats
     * @param blocksToConfirm the number of blocks it took this transaction to confirm
     * @param val the feerate of the transaction
     * @warning blocksToConfirm is 1-based and has to be >= 1
     */
    void Record(int blocksToConfirm, double val);

    /** Record a new transaction entering the mempool*/
    unsigned int NewTx(unsigned int nBlockHeight, double val);

    /** Remove a transaction from mempool tracking stats*/
    void removeTx(unsigned int entryHeight, unsigned int nBestSeenHeight,
                  unsigned int bucketIndex, bool inBlock);

    /** Update our estimates by decaying our historical moving average and updating
        with the data gathered from the current block */
    void UpdateMovingAverages();

    /**
     * Calculate a feerate estimate.  Find the lowest value bucket (or range of buckets
     * to make sure we have enough data points) whose transactions still have sufficient likelihood
     * of being confirmed within the target number of confirmations
     * @param confTarget target number of confirmations
     * @param sufficientTxVal required average number of transactions per block in a bucket range
     * @param minSuccess the success probability we require
     * @param nBlockHeight the current block height
     */
    double EstimateMedianVal(int confTarget, double sufficientTxVal,
                             double minSuccess, unsigned int nBlockHeight,
                             EstimationResult *result = nullptr) const;

    /** Return the max number of confirms we're tracking */
    unsigned int GetMaxConfirms() const { return scale * confAvg.size(); }

    /** Write state of estimation data to a file*/
    void Write(CAutoFile& fileout) const;

    /**
     * Read saved state of estimation data from a file and replace all internal data structures and
     * variables with this state.
     */
    void Read(CAutoFile& filein, int nFileVersion, size_t numBuckets);
};


TxConfirmStats::TxConfirmStats(const std::vector<double>& defaultBuckets,
                                const std::map<double, unsigned int>& defaultBucketMap,
                               unsigned int maxPeriods, double _decay, unsigned int _scale)
    : buckets(defaultBuckets), bucketMap(defaultBucketMap), decay(_decay), scale(_scale)
{
    assert(_scale != 0 && "_scale must be non-zero");
    confAvg.resize(maxPeriods);
    failAvg.resize(maxPeriods);
    for (unsigned int i = 0; i < maxPeriods; i++) {
        confAvg[i].resize(buckets.size());
        failAvg[i].resize(buckets.size());
    }

    txCtAvg.resize(buckets.size());
    m_feerate_avg.resize(buckets.size());

    resizeInMemoryCounters(buckets.size());
}

void TxConfirmStats::resizeInMemoryCounters(size_t newbuckets) {
    // newbuckets must be passed in because the buckets referred to during Read have not been updated yet.
    unconfTxs.resize(GetMaxConfirms());
    for (unsigned int i = 0; i < unconfTxs.size(); i++) {
        unconfTxs[i].resize(newbuckets);
    }
    oldUnconfTxs.resize(newbuckets);
}

// Roll the unconfirmed txs circular buffer
void TxConfirmStats::ClearCurrent(unsigned int nBlockHeight)
{
    for (unsigned int j = 0; j < buckets.size(); j++) {
        oldUnconfTxs[j] += unconfTxs[nBlockHeight % unconfTxs.size()][j];
        unconfTxs[nBlockHeight%unconfTxs.size()][j] = 0;
    }
}


void TxConfirmStats::Record(int blocksToConfirm, double feerate)
{
    // blocksToConfirm is 1-based
    if (blocksToConfirm < 1)
        return;
    int periodsToConfirm = (blocksToConfirm + scale - 1) / scale;
    unsigned int bucketindex = bucketMap.lower_bound(feerate)->second;
    for (size_t i = periodsToConfirm; i <= confAvg.size(); i++) {
        confAvg[i - 1][bucketindex]++;
    }
    txCtAvg[bucketindex]++;
    m_feerate_avg[bucketindex] += feerate;
}

void TxConfirmStats::UpdateMovingAverages()
{
    assert(confAvg.size() == failAvg.size());
    for (unsigned int j = 0; j < buckets.size(); j++) {
        for (unsigned int i = 0; i < confAvg.size(); i++) {
            confAvg[i][j] *= decay;
            failAvg[i][j] *= decay;
        }
        m_feerate_avg[j] *= decay;
        txCtAvg[j] *= decay;
    }
}

// returns -1 on error conditions
double TxConfirmStats::EstimateMedianVal(int confTarget, double sufficientTxVal,
                                         double successBreakPoint, unsigned int nBlockHeight,
                                         EstimationResult *result) const
{
    // Counters for a bucket (or range of buckets)
    double nConf = 0; // Number of tx's confirmed within the confTarget
    double totalNum = 0; // Total number of tx's that were ever confirmed
    int extraNum = 0;  // Number of tx's still in mempool for confTarget or longer
    double failNum = 0; // Number of tx's that were never confirmed but removed from the mempool after confTarget
    const int periodTarget = (confTarget + scale - 1) / scale;
    const int maxbucketindex = buckets.size() - 1;

    // We'll combine buckets until we have enough samples.
    // The near and far variables will define the range we've combined
    // The best variables are the last range we saw which still had a high
    // enough confirmation rate to count as success.
    // The cur variables are the current range we're counting.
    unsigned int curNearBucket = maxbucketindex;
    unsigned int bestNearBucket = maxbucketindex;
    unsigned int curFarBucket = maxbucketindex;
    unsigned int bestFarBucket = maxbucketindex;

    bool foundAnswer = false;
    unsigned int bins = unconfTxs.size();
    bool newBucketRange = true;
    bool passing = true;
    EstimatorBucket passBucket;
    EstimatorBucket failBucket;

    // Start counting from highest feerate transactions
    for (int bucket = maxbucketindex; bucket >= 0; --bucket) {
        if (newBucketRange) {
            curNearBucket = bucket;
            newBucketRange = false;
        }
        curFarBucket = bucket;
        nConf += 