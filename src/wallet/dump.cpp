// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/dump.h>

#include <util/translation.h>
#include <wallet/wallet.h>

static const std::string DUMP_MAGIC = "BITCOIN_CORE_WALLET_DUMP";
uint32_t DUMP_VERSION = 1;

bool DumpWallet(CWallet& wallet, bilingual_str& error)
{
    // Get the dumpfile
    std::string dump_filename = gArgs.GetArg("-dumpfile", "");
    if (dump_filename.empty()) {
        error = _("No dump file provided. To use dump, -dumpfile=<filename> must be provided.");
        return false;
    }

    fs::path path = fs::PathFromString(dump_filename);
    path = fs::absolute(path);
    if (fs::exists(path)) {
        error = strprintf(_("File %s already exists. If you are sure this is what you want, move it out of the way first."), fs::PathToString(path));
        return false;
    }
    fsbridge::ofstream dump_file;
    dump_file.open(path);
    if (dump_file.fail()) {
        error = strprintf(_("Unable to open %s for writing"), fs::PathToString(path));
        return false;
    }

    CHashWriter hasher(0, 0);

    WalletDatabase& db = wallet.GetDatabase();
    std::unique_ptr<DatabaseBatch> batch = db.MakeBatch();

    bool ret = true;
    if (!batch->StartCursor()) {
        error = _("Error: Couldn't create cursor into database");
        ret = false;
    }

    // Write out a magic string with version
    std::string line = strprintf("%s,%u\n", DUMP_MAGIC, DUMP_VERSION);
    dump_file.write(line.data(), line.size());
    hasher.write(line.data(), line.size());

    // Write out the file format
    line = strprintf("%s,%s\n", "format", db.Format());
    dump_file.write(line.data(), line.size());
    hasher.write(line.data(), line.size());

    if (ret) {

        // Read the records
        while (true) {
            CDataStream ss_key(SER_DISK, CLIENT_VERSION);
            CDataStream ss_value(SER_DISK, CLIENT_VERSION);
            bool complete;
            ret = batch->ReadAtCursor(ss_key, ss_value, complete);
            if (complete) {
                ret = true;
                break;
            } else if (!ret) {
                error = _("Error reading next record from wallet database");
                break;
            }
            std::string key_str = HexStr(ss_key);
            std::string value_str = HexStr(ss_value);
            line = strprintf("%s,%s\n", key_str, value_str);
            dump_file.write(line.data(), line.size());
            hasher.write(line.data(), line.size());
        }
    }

    batch->CloseCursor();
    batch.reset();

    // Close the wallet after we're done with it. The caller won't be doing this
    wallet.Close();

    if (ret) {
        // Write the hash
        tfm::format(dump_file, "checksum,%s\n", HexStr(hasher.GetHash()));
        dump_file.close();
    } else {
        // Remove the dumpfile on failure
        dump_file.close();
        fs::remove(path);
    }

    return ret;
}

// The standard wallet deleter function blocks on the validation interface
// queue, which doesn't exist for the bitcoin-wallet. Define our own
// deleter here.
static void WalletToolReleaseWallet(CWallet* wallet)
{
    wallet->WalletLogPrintf("Releasing wallet\n");
    wallet->Close();
    delete wallet;
}

bool CreateFromDump(const std::string& name, const fs::path& wallet_path, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    // Get the dumpfile
    std::string dump_filename = gArgs.GetArg("-dumpfile", "");
    if (dump_filename.empty()) {
        error = _("No dump file provided. To use createfromdump, -dumpfile=<filename> must be provided.");
        return false;
    }

    fs::path dump_path = fs::PathFromString(dump_filename);
    dump_path = fs::absolute(dump_path);
    if (!fs::exists(dump_path)) {
        error = strprintf(_("Dump file %s does not exist."), fs::PathToString(dump_path));
        return false;
    }
    fsbridge::ifstream dump_file(dump_path);

    // Compute the checksum
    CHashWriter hasher(0, 0);
    uint256 checksum;

    // Check the magic and version
  