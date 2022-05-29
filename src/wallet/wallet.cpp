// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <chain.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <external_signer.h>
#include <fs.h>
#include <interfaces/chain.h>
#include <interfaces/wallet.h>
#include <key.h>
#include <key_io.h>
#include <outputtype.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <txmempool.h>
#include <util/bip32.h>
#include <util/check.h>
#include <util/error.h>
#include <util/fees.h>
#include <util/moneystr.h>
#include <util/rbf.h>
#include <util/string.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/context.h>
#include <wallet/fees.h>
#include <wallet/external_signer_scriptpubkeyman.h>

#include <univalue.h>

#include <algorithm>
#include <assert.h>
#include <optional>

#include <boost/algorithm/string/replace.hpp>

using interfaces::FoundBlock;

const std::map<uint64_t,std::string> WALLET_FLAG_CAVEATS{
    {WALLET_FLAG_AVOID_REUSE,
        "You need to rescan the blockchain in order to correctly mark used "
        "destinations in the past. Until this is done, some destinations may "
        "be considered unused, even if the opposite is the case."
    },
};

bool AddWalletSetting(interfaces::Chain& chain, const std::string& wallet_name)
{
    util::SettingsValue setting_value = chain.getRwSetting("wallet");
    if (!setting_value.isArray()) setting_value.setArray();
    for (const util::SettingsValue& value : setting_value.getValues()) {
        if (value.isStr() && value.get_str() == wallet_name) return true;
    }
    setting_value.push_back(wallet_name);
    return chain.updateRwSetting("wallet", setting_value);
}

bool RemoveWalletSetting(interfaces::Chain& chain, const std::string& wallet_name)
{
    util::SettingsValue setting_value = chain.getRwSetting("wallet");
    if (!setting_value.isArray()) return true;
    util::SettingsValue new_value(util::SettingsValue::VARR);
    for (const util::SettingsValue& value : setting_value.getValues()) {
        if (!value.isStr() || value.get_str() != wallet_name) new_value.push_back(value);
    }
    if (new_value.size() == setting_value.size()) return true;
    return chain.updateRwSetting("wallet", new_value);
}

static void UpdateWalletSetting(interfaces::Chain& chain,
                                const std::string& wallet_name,
                                std::optional<bool> load_on_startup,
                                std::vector<bilingual_str>& warnings)
{
    if (!load_on_startup) return;
    if (load_on_startup.value() && !AddWalletSetting(chain, wallet_name)) {
        warnings.emplace_back(Untranslated("Wallet load on startup setting could not be updated, so wallet may not be loaded next node startup."));
    } else if (!load_on_startup.value() && !RemoveWalletSetting(chain, wallet_name)) {
        warnings.emplace_back(Untranslated("Wallet load on startup setting could not be updated, so wallet may still be loaded next node startup."));
    }
}

/**
 * Refresh mempool status so the wallet is in an internally consistent state and
 * immediately knows the transaction's status: Whether it can be considered
 * trusted and is eligible to be abandoned ...
 */
static void RefreshMempoolStatus(CWalletTx& tx, interfaces::Chain& chain)
{
    if (chain.isInMempool(tx.GetHash())) {
        tx.m_state = TxStateInMempool();
    } else if (tx.state<TxStateInMempool>()) {
        tx.m_state = TxStateInactive();
    }
}

bool AddWallet(WalletContext& context, const std::shared_ptr<CWallet>& wallet)
{
    LOCK(context.wallets_mutex);
    assert(wallet);
    std::vector<std::shared_ptr<CWallet>>::const_iterator i = std::find(context.wallets.begin(), context.wallets.end(), wallet);
    if (i != context.wallets.end()) return false;
    context.wallets.push_back(wallet);
    wallet->ConnectScriptPubKeyManNotifiers();
    wallet->NotifyCanGetAddressesChanged();
    return true;
}

bool RemoveWallet(WalletContext& context, const std::shared_ptr<CWallet>& wallet, std::optional<bool> load_on_start, std::vector<bilingual_str>& warnings)
{
    assert(wallet);

    interfaces::Chain& chain = wallet->chain();
    std::string name = wallet->GetName();

    // Unregister with the validation interface which also drops shared ponters.
    wallet->m_chain_notifications_handler.reset();
    LOCK(context.wallets_mutex);
    std::vector<std::shared_ptr<CWallet>>::iterator i = std::find(context.wallets.begin(), context.wallets.end(), wallet);
    if (i == context.wallets.end()) return false;
    context.wallets.erase(i);

    // Write the wallet setting
    UpdateWalletSetting(chain, name, load_on_start, warnings);

    return true;
}

bool RemoveWallet(WalletContext& context, const std::shared_ptr<CWallet>& wallet, std::optional<bool> load_on_start)
{
    std::vector<bilingual_str> warnings;
    return RemoveWallet(context, wallet, load_on_start, warnings);
}

std::vector<std::shared_ptr<CWallet>> GetWallets(WalletContext& context)
{
    LOCK(context.wallets_mutex);
    return context.wallets;
}

std::shared_ptr<CWallet> GetWallet(WalletContext& context, const std::string& name)
{
    LOCK(context.wallets_mutex);
    for (const std::shared_ptr<CWallet>& wallet : context.wallets) {
        if (wallet->GetName() == name) return wallet;
    }
    return nullptr;
}

std::unique_ptr<interfaces::Handler> HandleLoadWallet(WalletContext& context, LoadWalletFn load_wallet)
{
    LOCK(context.wallets_mutex);
    auto it = context.wallet_load_fns.emplace(context.wallet_load_fns.end(), std::move(load_wallet));
    return interfaces::MakeHandler([&context, it] { LOCK(context.wallets_mutex); context.wallet_load_fns.erase(it); });
}

static Mutex g_loading_wallet_mutex;
static Mutex g_wallet_release_mutex;
static std::condition_variable g_wallet_release_cv;
static std::set<std::string> g_loading_wallet_set GUARDED_BY(g_loading_wallet_mutex);
static std::set<std::string> g_unloading_wallet_set GUARDED_BY(g_wallet_release_mutex);

// Custom deleter for shared_ptr<CWallet>.
static void ReleaseWallet(CWallet* wallet)
{
    const std::string name = wallet->GetName();
    wallet->WalletLogPrintf("Releasing wallet\n");
    wallet->Flush();
    delete wallet;
    // Wallet is now released, notify UnloadWallet, if any.
    {
        LOCK(g_wallet_release_mutex);
        if (g_unloading_wallet_set.erase(name) == 0) {
            // UnloadWallet was not called for this wallet, all done.
            return;
        }
    }
    g_wallet_release_cv.notify_all();
}

void UnloadWallet(std::shared_ptr<CWallet>&& wallet)
{
    // Mark wallet for unloading.
    const std::string name = wallet->GetName();
    {
        LOCK(g_wallet_release_mutex);
        auto it = g_unloading_wallet_set.insert(name);
        assert(it.second);
    }
    // The wallet can be in use so it's not possible to explicitly unload here.
    // Notify the unload intent so that all remaining shared pointers are
    // released.
    wallet->NotifyUnload();

    // Time to ditch our shared_ptr and wait for ReleaseWallet call.
    wallet.reset();
    {
        WAIT_LOCK(g_wallet_release_mutex, lock);
        while (g_unloading_wallet_set.count(name) == 1) {
            g_wallet_release_cv.wait(lock);
        }
    }
}

namespace {
std::shared_ptr<CWallet> LoadWalletInternal(WalletContext& context, const std::string& name, std::optional<bool> load_on_start, const DatabaseOptions& options, DatabaseStatus& status, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    try {
        std::unique_ptr<WalletDatabase> database = MakeWalletDatabase(name, options, status, error);
        if (!database) {
            error = Untranslated("Wallet file verification failed.") + Untranslated(" ") + error;
            return nullptr;
        }

        context.chain->initMessage(_("Loading wallet…").translated);
        const std::shared_ptr<CWallet> wallet = CWallet::Create(context, name, std::move(database), options.create_flags, error, warnings);
        if (!wallet) {
            error = Untranslated("Wallet loading failed.") + Untranslated(" ") + error;
            status = DatabaseStatus::FAILED_LOAD;
            return nullptr;
        }
        AddWallet(context, wallet);
        wallet->postInitProcess();

        // Write the wallet setting
        UpdateWalletSetting(*context.chain, name, load_on_start, warnings);

        return wallet;
    } catch (const std::runtime_error& e) {
        error = Untranslated(e.what());
        status = DatabaseStatus::FAILED_LOAD;
        return nullptr;
    }
}
} // namespace

std::shared_ptr<CWallet> LoadWallet(WalletContext& context, const std::string& name, std::optional<bool> load_on_start, const DatabaseOptions& options, DatabaseStatus& status, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    auto result = WITH_LOCK(g_loading_wallet_mutex, return g_loading_wallet_set.insert(name));
    if (!result.second) {
        error = Untranslated("Wallet already loading.");
        status = DatabaseStatus::FAILED_LOAD;
        return nullptr;
    }
    auto wallet = LoadWalletInternal(context, name, load_on_start, options, status, error, warnings);
    WITH_LOCK(g_loading_wallet_mutex, g_loading_wallet_set.erase(result.first));
    return wallet;
}

std::shared_ptr<CWallet> CreateWallet(WalletContext& context, const std::string& name, std::optional<bool> load_on_start, DatabaseOptions& options, DatabaseStatus& status, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    uint64_t wallet_creation_flags = options.create_flags;
    const SecureString& passphrase = options.create_passphrase;

    if (wallet_creation_flags & WALLET_FLAG_DESCRIPTORS) options.require_format = DatabaseFormat::SQLITE;

    // Indicate that the wallet is actually supposed to be blank and not just blank to make it encrypted
    bool create_blank = (wallet_creation_flags & WALLET_FLAG_BLANK_WALLET);

    // Born encrypted wallets need to be created blank first.
    if (!passphrase.empty()) {
        wallet_creation_flags |= WALLET_FLAG_BLANK_WALLET;
    }

    // Private keys must be disabled for an external signer wallet
    if ((wallet_creation_flags & WALLET_FLAG_EXTERNAL_SIGNER) && !(wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        error = Untranslated("Private keys must be disabled when using an external signer");
        status = DatabaseStatus::FAILED_CREATE;
        return nullptr;
    }

    // Descriptor support must be enabled for an external signer wallet
    if ((wallet_creation_flags & WALLET_FLAG_EXTERNAL_SIGNER) && !(wallet_creation_flags & WALLET_FLAG_DESCRIPTORS)) {
        error = Untranslated("Descriptor support must be enabled when using an external signer");
        status = DatabaseStatus::FAILED_CREATE;
        return nullptr;
    }

    // Wallet::Verify will check if we're trying to create a wallet with a duplicate name.
    std::unique_ptr<WalletDatabase> database = MakeWalletDatabase(name, options, status, error);
    if (!database) {
        error = Untranslated("Wallet file verification failed.") + Untranslated(" ") + error;
        status = DatabaseStatus::FAILED_VERIFY;
        return nullptr;
    }

    // Do not allow a passphrase when private keys are disabled
    if (!passphrase.empty() && (wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        error = Untranslated("Passphrase provided but private keys are disabled. A passphrase is only used to encrypt private keys, so cannot be used for wallets with private keys disabled.");
        status = DatabaseStatus::FAILED_CREATE;
        return nullptr;
    }

    // Make the wallet
    context.chain->initMessage(_("Loading wallet…").translated);
    const std::shared_ptr<CWallet> wallet = CWallet::Create(context, name, std::move(database), wallet_creation_flags, error, warnings);
    if (!wallet) {
        error = Untranslated("Wallet creation failed.") + Untranslated(" ") + error;
        status = DatabaseStatus::FAILED_CREATE;
        return nullptr;
    }

    // Encrypt the wallet
    if (!passphrase.empty() && !(wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        if (!wallet->EncryptWallet(passphrase)) {
            error = Untranslated("Error: Wallet created but failed to encrypt.");
            status = DatabaseStatus::FAILED_ENCRYPT;
            return nullptr;
        }
        if (!create_blank) {
            // Unlock the wallet
            if (!wallet->Unlock(passphrase)) {
                error = Untranslated("Error: Wallet was encrypted but could not be unlocked");
                status = DatabaseStatus::FAILED_ENCRYPT;
                return nullptr;
            }

            // Set a seed for the wallet
            {
                LOCK(wallet->cs_wallet);
                if (wallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS)) {
                    wallet->SetupDescriptorScriptPubKeyMans();
                } else {
                    for (auto spk_man : wallet->GetActiveScriptPubKeyMans()) {
                        if (!spk_man->SetupGeneration()) {
                            error = Untranslated("Unable to generate initial keys");
                            status = DatabaseStatus::FAILED_CREATE;
                            return nullptr;
                        }
                    }
                }
            }

            // Relock the wallet
            wallet->Lock();
        }
    }
    AddWallet(context, wallet);
    wallet->postInitProcess();

    // Write the wallet settings
    UpdateWalletSetting(*context.chain, name, load_on_start, warnings);

    status = DatabaseStatus::SUCCESS;
    return wallet;
}

std::shared_ptr<CWallet> RestoreWallet(WalletContext& context, const std::string& backup_file, const std::string& wallet_name, std::optional<bool> load_on_start, DatabaseStatus& status, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    DatabaseOptions options;
    options.require_existing = true;

    if (!fs::exists(fs::u8path(backup_file))) {
        error = Untranslated("Backup file does not exist");
        status = DatabaseStatus::FAILED_INVALID_BACKUP_FILE;
        return nullptr;
    }

    const fs::path wallet_path = fsbridge::AbsPathJoin(GetWalletDir(), fs::u8path(wallet_name));

    if (fs::exists(wallet_path) || !TryCreateDirectories(wallet_path)) {
        error = Untranslated(strprintf("Failed to create database path '%s'. Database already exists.", fs::PathToString(wallet_path)));
        status = DatabaseStatus::FAILED_ALREADY_EXISTS;
        return nullptr;
    }

    auto wallet_file = wallet_path / "wallet.dat";
    fs::copy_file(backup_file, wallet_file, fs::copy_option::fail_if_exists);

    auto wallet = LoadWallet(context, wallet_name, load_on_start, options, status, error, warnings);

    if (!wallet) {
        fs::remove(wallet_file);
        fs::remove(wallet_path);
    }

    return wallet;
}

/** @defgroup mapWallet
 *
 * @{
 */

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    AssertLockHeld(cs_wallet);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return nullptr;
    return &(it->second);
}

void CWallet::UpgradeKeyMetadata()
{
    if (IsLocked() || IsWalletFlagSet(WALLET_FLAG_KEY_ORIGIN_METADATA)) {
        return;
    }

    auto spk_man = GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        return;
    }

    spk_man->UpgradeKeyMetadata();
    SetWalletFlag(WALLET_FLAG_KEY_ORIGIN_METADATA);
}

void CWallet::UpgradeDescriptorCache()
{
    if (!IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS) || IsLocked() || IsWalletFlagSet(WALLET_FLAG_LAST_HARDENED_XPUB_CACHED)) {
        return;
    }

    for (ScriptPubKeyMan* spkm : GetAllScriptPubKeyMans()) {
        DescriptorScriptPubKeyMan* desc_spkm = dynamic_cast<DescriptorScriptPubKeyMan*>(spkm);
        desc_spkm->UpgradeDescriptorCache();
    }
    SetWalletFlag(WALLET_FLAG_LAST_HARDENED_XPUB_CACHED);
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool accept_no_keys)
{
    CCrypter crypter;
    CKeyingMaterial _vMasterKey;

    {
        LOCK(cs_wallet);
        for (const MasterKeyMap::value_type& pMasterKey : mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                continue; // try another master key
            if (Unlock(_vMasterKey, accept_no_keys)) {
                // Now that we've unlocked, upgrade the key metadata
                UpgradeKeyMetadata();
                // Now that we've unlocked, upgrade the descriptor cache
                UpgradeDescriptorCache();
                return true;
            }
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial _vMasterKey;
        for (MasterKeyMap::value_type& pMasterKey : mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                return false;
            if (Unlock(_vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = static_cast<unsigned int>(pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime))));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + static_cast<unsigned int>(pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime)))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                WalletLogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(_vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                WalletBatch(GetDatabase()).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::chainStateFlushed(const CBlockLocator& loc)
{
    WalletBatch batch(GetDatabase());
    batch.WriteBestBlock(loc);
}

void CWallet::SetMinVersion(enum WalletFeature nVersion, WalletBatch* batch_in)
{
    LOCK(cs_wallet);
    if (nWalletVersion >= nVersion)
        return;
    nWalletVersion = nVersion;

    {
        WalletBatch* batch = batch_in ? batch_in : new WalletBatch(GetDatabase());
        if (nWalletVersion > 40000)
            batch->WriteMinVersion(nWalletVersion);
        if (!batch_in)
            delete batch;
    }
}

std::set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    std::set<uint256> result;
    AssertLockHeld(cs_wallet);

    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(txid);
    if (it == mapWallet.end())
        return result;
    const CWalletTx& wtx = it->second;

    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    for (const CTxIn& txin : wtx.tx->vin)
    {
        if (mapTxSpends.count(txin.prevout) <= 1)
            continue;  // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator _it = range.first; _it != range.second; ++_it)
            result.insert(_it->second);
    }
    return result;
}

bool CWallet::HasWalletSpend(const uint256& txid) const
{
    AssertLockHeld(cs_wallet);
    auto iter = mapTxSpends.lower_bound(COutPoint(txid, 0));
    return (iter != mapTxSpends.end() && iter->first.hash == txid);
}

void CWallet::Flush()
{
    GetDatabase().Flush();
}

void CWallet::Close()
{
    GetDatabase().Close();
}

void CWallet::SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTx* copyFrom = nullptr;
    for (TxSpends::iterator it = range.first; it != range.second; ++it) {
        const CWalletTx* wtx = &mapWallet.at(it->second);
        if (wtx->nOrderPos < nMinOrderPos) {
            nMinOrderPos = wtx->nOrderPos;
            copyFrom = wtx;
        }
    }

    if (!copyFrom) {
        return;
    }

    // Now copy data from copyFrom to rest:
    for (TxSpends::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        CWalletTx* copyTo = &mapWallet.at(hash);
        if (copyFrom == copyTo) continue;
        assert(copyFrom && "Oldest wallet transaction in range assumed to have been found.");
        if (!copyFrom->IsEquivalentTo(*copyTo)) continue;
        copyTo->mapValue = copyFrom->mapValue;
        copyTo->vOrderForm = copyFrom->vOrderForm;
        // fTimeReceivedIsTxTime not copied on purpose
        // nTimeReceived not copied on purpose
        copyTo->nTimeSmart = copyFrom->nTimeSmart;
        copyTo->fFromMe = copyFrom->fFromMe;
        // nOrderPos not copied on purpose
        // cached members not copied on purpose
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    for (TxSpends::const_iter