// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/bdb.h>
#include <wallet/db.h>

#include <util/strencodings.h>
#include <util/translation.h>

#include <stdint.h>

#ifndef WIN32
#include <sys/stat.h>
#endif

namespace {

//! Make sure database has a unique fileid within the environment. If it
//! doesn't, throw an error. BDB caches do not work properly when more than one
//! open database has the same fileid (values written to one database may show
//! up in reads to other databases).
//!
//! BerkeleyDB generates unique fileids by default
//! (https://docs.oracle.com/cd/E17275_01/html/programmer_reference/program_copy.html),
//! so bitcoin should never create different databases with the same fileid, but
//! this error can be triggered if users manually copy database files.
void CheckUniqueFileid(const BerkeleyEnvironment& env, const std::string& filename, Db& db, WalletDatabaseFileId& fileid)
{
    if (env.IsMock()) return;

    int ret = db.get_mpf()->get_fileid(fileid.value);
    if (ret != 0) {
        throw std::runtime_error(strprintf("BerkeleyDatabase: Can't open database %s (get_fileid failed with %d)", filename, ret));
    }

    for (const auto& item : env.m_fileids) {
        if (fileid == item.second && &fileid != &item.second) {
            throw std::runtime_error(strprintf("BerkeleyDatabase: Can't open database %s (duplicates fileid %s from %s)", filename,
                HexStr(item.second.value), item.first));
        }
    }
}

RecursiveMutex cs_db;
std::map<std::string, std::weak_ptr<BerkeleyEnvironment>> g_dbenvs GUARDED_BY(cs_db); //!< Map from directory name to db environment.
} // namespace

bool WalletDatabaseFileId::operator==(const WalletDatabaseFileId& rhs) const
{
    return memcmp(value, &rhs.value, sizeof(value)) == 0;
}

/**
 * @param[in] env_directory Path to environment directory
 * @return A shared pointer to the BerkeleyEnvironment object for the wallet directory, never empty because ~BerkeleyEnvironment
 * erases the weak pointer from the g_dbenvs map.
 * @post A new BerkeleyEnvironment weak pointer is inserted into g_dbenvs if the directory path key was not already in the map.
 */
std::shared_ptr<BerkeleyEnvironment> GetBerkeleyEnv(const fs::path& env_directory)
{
    LOCK(cs_db);
    auto inserted = g_dbenvs.emplace(fs::PathToString(env_directory), std::weak_ptr<BerkeleyEnvironment>());
    if (inserted.second) {
        auto env = std::make_shared<BerkeleyEnvironment>(env_directory);
        inserted.first->second = env;
        return env;
    }
    return inserted.first->second.lock();
}

//
// BerkeleyBatch
//

void BerkeleyEnvironment::Close()
{
    if (!fDbEnvInit)
        return;

    fDbEnvInit = false;

    for (auto& db : m_databases) {
        BerkeleyDatabase& database = db.second.get();
        assert(database.m_refcount <= 0);
        if (database.m_db) {
            database.m_db->close(0);
            database.m_db.reset();
        }
    }

    FILE* error_file = nullptr;
    dbenv->get_errfile(&error_file);

    int ret = dbenv->close(0);
    if (ret != 0)
        LogPrintf("BerkeleyEnvironment::Close: Error %d closing database environment: %s\n", ret, DbEnv::strerror(ret));
    if (!fMockDb)
        DbEnv((u_int32_t)0).remove(strPath.c_str(), 0);

    if (error_file) fclose(error_file);

    UnlockDirectory(fs::PathFromString(strPath), ".walletlock");
}

void BerkeleyEnvironment::Reset()
{
    dbenv.reset(new DbEnv(DB_CXX_NO_EXCEPTIONS));
    fDbEnvInit = false;
    fMockDb = false;
}

BerkeleyEnvironment::BerkeleyEnvironment(const fs::path& dir_path) : strPath(fs::PathToString(dir_path))
{
    Reset();
}

BerkeleyEnvironment::~BerkeleyEnvironment()
{
    LOCK(cs_db);
    g_dbenvs.erase(strPath);
    Close();
}

bool BerkeleyEnvironment::Open(bilingual_str& err)
{
    if (fDbEnvInit) {
        return true;
    }

    fs::path pathIn = fs::PathFromString(strPath);
    TryCreateDirectories(pathIn);
    if (!LockDirectory(pathIn, ".walletlock")) {
        LogPrintf("Cannot obtain a lock on wallet directory %s. Another instance may be using it.\n", strPath);
        err = strprintf(_("Error initializing wallet database environment %s!"), fs::quoted(fs::PathToString(Directory())));
        return false;
    }

    fs::path pathLogDir = pathIn / "database";
    TryCreateDirectories(pathLogDir);
    fs::path pathErrorFile = pathIn / "db.log";
    LogPrintf("BerkeleyEnvironment::Open: LogDir=%s ErrorFile=%s\n", fs::PathToString(pathLogDir), fs::PathToString(pathErrorFile));

    unsigned int nEnvFlags = 0;
    if (gArgs.GetBoolArg("-privdb", DEFAULT_WALLET_PRIVDB))
        nEnvFlags |= DB_PRIVATE;

    dbenv->set_lg_dir(fs::PathToString(pathLogDir).c_str());
    dbenv->set_cachesize(0, 0x100000, 1); // 1 MiB should be enough for just the wallet
    dbenv->set_lg_bsize(0x10000);
    dbenv->set_lg_max(1048576);
    dbenv->set_lk_max_locks(40000);
    dbenv->set_lk_max_objects(40000);
    dbenv->set_errfile(fsbridge::fopen(pathErrorFile, "a")); /// debug
    dbenv->set_flags(DB_AUTO_COMMIT, 1);
    dbenv->set_flags(DB_TXN_WRITE_NOSYNC, 1);
    dbenv->log_set_config(DB_LOG_AUTO_REMOVE, 1);
    int ret = dbenv->open(strPath.c_str(),
                         DB_CREATE |
                             DB_INIT_LOCK |
                             DB_INIT_LOG |
                             DB_INIT_MPOOL |
                             DB_INIT_TXN |
                             DB_THREAD |
                             DB_RECOVER |
                             nEnvFlags,
                         S_IRUSR | S_IWUSR);
    if (ret != 0) {
        LogPrintf("BerkeleyEnvironment::Open: Error %d opening database environment: %s\n", ret, DbEnv::strerror(ret));
        int ret2 = dbenv->close(0);
        if (ret2 != 0) {
            LogPrintf("BerkeleyEnvironment::Open: Error %d closing failed database environment: %s\n", ret2, DbEnv::strerror(ret2));
        }
        Reset();
        err = strprintf(_("Error initializing wallet database environment %s!"), fs::quoted(fs::PathToString(Directory())));
        if (ret == DB_RUNRECOVERY) {
            err += Untranslated(" ") + _("This error could occur if this wallet was not shutdown cleanly and was last loaded using a build with a newer version of Berkeley DB. If so, please use the software that last loaded this wallet");
        }
        return false;
    }

    fDbEnvInit = true;
    fMockDb = false;
    return true;
}

//! Construct an in-memory mock Berkeley environment for testing
BerkeleyEnvironment::BerkeleyEnvironment()
{
    Reset();

    LogPrint(BCLog::WALLETDB, "BerkeleyEnvironment::MakeMock\n");

    dbenv->set_cachesize(1, 0, 1);
    dbenv->set_lg_bsize(10485760 * 4);
    dbenv->set_lg_max(10485760);
    dbenv->set_lk_max_locks(10000);
    dbenv->set_lk_max_objects(10000);
    dbenv->set_flags(DB_AUTO_COMMIT, 1);
    dbenv->log_set_config(DB_LOG_IN_MEMORY, 1);
    int ret = dbenv->open(nullptr,
                         DB_CREATE |
                             DB_INIT_LOCK |
                             DB_INIT_LOG |
                             DB_INIT_MPOOL |
                             DB_INIT_TXN |
                             DB_THREAD |
                             DB_PRIVATE,
                         S_IRUSR | S_IWUSR);
    if (ret > 0) {
        throw std::runtime_error(strprintf("BerkeleyEnvironment::MakeMock: Error %d opening database environment.", ret));
    }

    fDbEnvInit = true;
    fMockDb = true;
}

BerkeleyBatch::SafeDbt::SafeDbt()
{
    m_dbt.set_flags(DB_DBT_MALLOC);
}

BerkeleyBatch::SafeDbt::SafeDbt(void* data, size_t size)
    : m_dbt(data, size)
{
}

BerkeleyBatch::SafeDbt::~SafeDbt()
{
    if (m_dbt.get_data() != nullptr) {
        // Clear memory, e.g. in case it was a private key
        memory_cleanse(m_dbt.get_data(), m_dbt.get_size());
        // under DB_DBT_MALLOC, data is malloced by the Dbt, but must be
        // freed by the caller.
        // https://docs.oracle.com/cd/E17275_01/html/api_reference/C/dbt.html
        if (m_dbt.get_flags() & DB_DBT_MALLOC) {
            free(m_dbt.get_data());
        }
    }
}

const void* BerkeleyBatch::SafeDbt::get_data() const
{
    return m_dbt.get_data();
}

u_int32_t BerkeleyBatch::SafeDbt::get_size() const
{
    return m_dbt.get_size();
}

BerkeleyBatch::SafeDbt::operator Dbt*()
{
    return &m_dbt;
}

bool BerkeleyDatabase::Verify(bilingual_str& errorStr)
{
    fs::path walletDir = env->Directory();
    fs::path file_path = walletDir / strFile;

    LogPrintf("Using BerkeleyDB version %s\n", BerkeleyDatabaseVersion());
    LogPrintf("Using wallet %s\n", fs::PathToString(file_path));

    if (!env->Open(errorStr)) {
        return false;
    }

    if (fs::exists(file_path))
    {
        assert(m_refcount == 0);

        Db db(env->dbenv.get(), 0);
        int result = db.verify(strFile.c_str(), nullptr, nullptr, 0);
        if (result != 0) {
            errorStr = strprintf(_("%s corrupt. Try using the wallet tool bitcoin-wallet to salvage or restoring a backup."), fs::quoted(fs::PathToString(file_path)));
            return false;
        }
    }
    // also return true if files does not exists
    return true;
}

void BerkeleyEnvironment::CheckpointLSN(const std::string& strFile)
{
    dbenv->txn_checkpoint(0, 0, 0);
    if (fMockDb)
        return;
    dbenv->lsn_reset(strFile.c_str(), 0);
}

BerkeleyDatabase::~BerkeleyDatabase()
{
    if (env) {
        LOCK(cs_db);
        env->CloseDb(strFile);
        assert(!m_db);
        size_t erased = env->