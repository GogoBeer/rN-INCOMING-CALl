// Copyright (c) 2014-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <wallet/crypter.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(wallet_crypto_tests, BasicTestingSetup)

class TestCrypter
{
public:
static void TestPassphraseSingle(const std::vector<unsigned char>& vchSalt, const SecureString& passphrase, uint32_t rounds,
                 const std::vector<unsigned char>& correctKey = std::vector<unsigned char>(),
                 const std::vector<unsigned char>& correctIV=std::vector<unsigned char>())
{
    CCrypter crypt;
    crypt.SetKeyFromPassphrase(passphrase, vchSalt, rounds, 0);

    if(!correctKey.empty())
        BOOST_CHECK_MESSAGE(memcmp(crypt.vchKey.data(), correctKey.data(), crypt.vchKey.size()) == 0, \
            HexStr(crypt.vchKey) + std::string(" != ") + HexStr(correctKey));
    if(!correctIV.empty())
        BOOST_CHECK_MESSAGE(memcmp(crypt.vchIV.data(), correctIV.data(), crypt.vchIV.size()) == 0,
            HexStr(crypt.vchIV) + std::string(" != ") + HexStr(correctIV));
}

static void TestPassphrase(const std::vector<unsigned char>& vchSalt, const SecureString& passphrase, uint32_t rounds,
                 const std::vector<unsigned char>& correctKey = std::vector<unsigned char>(),
                 const std::vector<unsigned char>& correctIV=std::vector<unsigned char>())
{
    TestPassphraseSingle(vchSalt, passphrase, rounds, correctKey, correctIV);
    for(SecureString::const_iterator i(passphrase.begin()); i != passphrase.end(); ++i)
        TestPassphraseSingle(vchSalt, SecureString(i, passphrase.end()), rounds);
}

static void TestDecrypt(const CCrypter& crypt, const std::vector<unsigned char>& vchCiphertext, \
                        const std::vector<unsigned char>& vchPlaintext = std::vector<unsigned char>())
{
    CKeyingMaterial vchDecrypted;
    crypt.Decrypt(vchCiphertext, vchDecrypted);
    if (vchPlaintext.size())
        BOOST_CHECK(CKeyingMaterial(vchPlaintext.begin(), vchPlaintext.end()) == vchDecrypted);
}

static void TestEncryptSingle(const CCrypter& crypt, const CKeyingMaterial& vchPlaintext,
                       const std::vector<unsigned char>& vchCiphertextCorrect = std::vector<unsigned char>())
{
    std::vector<unsigned char> vchCiphertext;
    crypt.Encrypt(vchPlaintext, vchCiphertext);

    i