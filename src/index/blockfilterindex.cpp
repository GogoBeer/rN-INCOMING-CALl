// Copyright (c) 2018-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <map>

#include <dbwrapper.h>
#include <index/blockfilterindex.h>
#include <node/blockstorage.h>
#include <util/system.h>

/* The index database stores three items for each block: the disk location of the encoded filter,
 * its dSHA256 hash, and the header. Those belonging to blocks on the active chain are indexed by
 * height, and those belonging to blocks that have been reorganized out of the active chain are
 * indexed by block hash. This ensures that filter data for any block that becomes part of the
 * active chain can always be retrieved, alleviating timing concerns.
 *
 * The filters themselves are stored in flat files and referenced by the LevelDB entries. This
 * minimizes the amount of data written to LevelDB and keeps the database values constant size. The
 * disk location of the next block filter to be written (represented as a FlatFilePos) is stored
 * under the DB_FILTER_POS key.
 *
 * Keys for the height index have the type [DB_BLOCK_HEIGHT, uint32 (BE)]. The height is represented
 * as big-endian so that sequential reads of filters by height are fast.
 * Keys for the hash index have the type [DB_BLOCK_HASH, uint256].
 */
constexpr uint8_t DB_BLOCK_HASH{'s'};
constexpr uint8_t DB_BLOCK_HEIGHT{'t'};
constexpr uint8_t DB_FILTER_POS{'P'};

constexpr unsigned int MAX_FLTR_FILE_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for fltr?????.dat files */
constexpr unsigned int FLTR_FILE_CHUNK_SIZE = 0x100000; // 1 MiB
/** Maximum size of the cfheaders cache
 *  We have a limit to prevent a bug in filling this cache
 *  potentially turning into an OOM. At 2000 entries, this cache
 *  is big enough for a 2,000,000 length block chain, which
 *  we should be enough until ~2047. */
constexpr size_t CF_HEADERS_CACHE_MAX_SZ{2000};

namespace {

struct DBVal {
    uint256 hash;
    uint256 header;
    FlatFilePos pos;

    SERIALIZE_METHODS(DBVal, obj) { READWRITE(obj.hash, obj.header, obj.pos); }
};

struct DBHeightKey {
    int height;

    explicit DBHeightKey(int height_in) : height(height_in) {}

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata8(s, DB_BLOCK_HEIGHT);
        ser_writedata32be(s, height);
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        const uint8_t prefix{ser_readdata8(s)};
        if (prefix != DB_BLOCK_HEIGHT) {
            throw std::ios_base::failure("Invalid format for block filter index DB height key");
        }
        height = ser_readdata32be(s);
    }
};

struct DBHashKey {
    uint256 hash;

    explicit DBHashKey(const uint256& hash_in) : hash(hash_in) {}

    SERIALIZE_METHODS(DBHashKey, obj) {
        uint8_t prefix{DB_BLOCK_HASH};
        READWRITE(prefix);
        if (prefix != DB_BLOCK_HASH) {
            throw std::ios_base::failure("Invalid format for block filter index DB hash key");
        }

        READWRITE(obj.hash);
    }
};

}; // namespace

static std::map<BlockFilterType, BlockFilterIndex> g_filter_indexes;

BlockFilterIndex::BlockFilterIndex(BlockFilterType filter_type,
                                   size_t n_cache_size, bool f_memory, bool f_wipe)
    : m_filter_type(filter_type)
{
    const std::string& filter_name = BlockFilterTypeName(filter_type);
    if (filter_name.empty()) throw std::invalid_argument("unknown filter_type");

    fs::path path = gArgs.GetDataDirNet() / "indexes" / "blockfilter" / filter_name;
    fs::create_directories(path);

    m_name = filter_name + " block filter index";
    m_db = std::make_unique<BaseIndex::DB>(path / "db", n_cache_size, f_memory, f_wipe);
    m_filter_fileseq = std::make_unique<FlatFileSeq>(std::move(path), "fltr", FLTR_FILE_CHUNK_SIZE);
}

bool BlockFilterIndex::Init()
{
    if (!m_db->Read(DB_FILTER_POS, m_next_filter_pos)) {
        // Check that the cause of the read failure is that the key does not exist. Any other errors
        // indicate database corruption or a disk failure, and starting the index would cause
        // further corruption.
        if (m_db->Exists(DB_FILTER_POS)) {
            return error("%s: Cannot read current %s state; index may be corrupted",
                         __func__, GetName());
        }

        // If the DB_FILTER_POS is not set, then initialize to the first location.
        m_next_filter_pos.nFile = 0;
        m_next_filter_pos.nPos = 0;
    }
    return BaseIndex::Init();
}

bool BlockFilterIndex::CommitInternal(CDBBatch& batch)
{
    const FlatFilePos& pos = m_next_filter_pos;

    // Flush current filter file to disk.
    CAutoFile file(m_filter_fileseq->Open(pos), SER_DISK, CLIENT_VERSION);
    if (file.IsNull()) {
        return error("%s: Failed to open filter file %d", __func__, pos.nFile);
    }
    if (!FileCommit(file.Get())) {
        return error("%s: Failed to commit filter file %d", __func__, pos.nFile);
    }

    batch.Write(DB_FILTER_POS, pos);
    return BaseIndex::CommitInternal(batch);
}

bool BlockFilterIndex::ReadFilterFromDisk(const FlatFilePos& pos, BlockFilter& filter) const
{
    CAutoFile filein(m_filter_fileseq->Open(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        return false;
    }

    uint256 block_hash;
    std::vector<uint8_t> encoded_filter;
    try {
        filein >> block_hash >> encoded_filter;
        filter = BlockFilter(GetFilterType(), block_hash, std::move(encoded_filter));
    }
    catch (const std::exception& e) {
        return error("%s: Failed to deserialize block filter from disk: %s", __func__, e.what());
    }

    return true;
}

size_t BlockFilterIndex::WriteFilterToDisk(FlatFilePos& pos, const BlockFilter& filter)
{
    assert(filter.GetFilterType() == GetFilterType());

    size_t data_size =
        GetSerializeSize(filter.GetBlockHash(), CLIENT_VERSION) +
        GetSerializeSize(filter.GetEncodedFilter(), CLIENT_VERSION);

    // If writing the filter would overflow the file, flush and move to the next one.
    if (pos.nPos + data_size > MAX_FLTR_FILE_SIZE) {
        CAutoFile last_file(m_filter_fileseq->Open(pos), SER_DISK, CLIENT_VERSION);
        if (last_file.IsNull()) {
            LogPrintf("%s: Failed to open filter file %d\n", __func__, pos.nFile);
            return 0;
        }
        if (!TruncateFile(last_file.Get(), pos.nPos)) {
            LogPrintf("%s: Failed to truncate filter file %d\n", __func__, pos.nFile);
            return 0;
        }
        if (!FileCommit(last_file.Get())) {
            LogPrintf("%s: Failed to commit filter file %d\n", __func__, pos.nFile);
            return 0;
        }

        pos.nFile++;
        pos.nPos = 0;
    }

    // Pre-allocate sufficient space for filter data.
    bool out_of_space;
    m_filter_fileseq->Allocate(pos, data_size, out_of_space);
    if (out_of_space) {
        LogPrintf("%s: out of disk space\n", __func__);
        return 0;
    }

    CAutoFile fileout(m_filter_fileseq->Open(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull()) {
        LogPrintf("%s: Failed to open filter file %d\n", __func__, pos.nFile);
        return 0;
    }

    fileout << filter.GetBlockHash() << filter.GetEncodedFilter();
    return data_size;
}

bool BlockFilterIndex::WriteBlock(const CBlock& block, const CBlockIndex* pindex)
{
    CBlockUndo block_undo;
    uint256 prev_header;

    if (pindex->nHeight > 0) {
        if (!UndoReadFromDisk(block_undo, pindex)) {
            return false;
        }

        std::pair<uint256, DBVal> read_out;
        if (!m_db->Read(DBHeigh