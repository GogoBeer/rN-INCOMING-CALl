// Copyright (c) 2014-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/aes.h>
#include <crypto/chacha20.h>
#include <crypto/chacha_poly_aead.h>
#include <crypto/hkdf_sha256_32.h>
#include <crypto/hmac_sha256.h>
#include <crypto/hmac_sha512.h>
#include <crypto/poly1305.h>
#include <crypto/ripemd160.h>
#include <crypto/sha1.h>
#include <crypto/sha256.h>
#include <crypto/sha3.h>
#include <crypto/sha512.h>
#include <crypto/muhash.h>
#include <random.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(crypto_tests, BasicTestingSetup)

template<typename Hasher, typename In, typename Out>
static void TestVector(const Hasher &h, const In &in, const Out &out) {
    Out hash;
    BOOST_CHECK(out.size() == h.OUTPUT_SIZE);
    hash.resize(out.size());
    {
        // Test that writing the whole input string at once works.
        Hasher(h).Write((const uint8_t*)in.data(), in.size()).Finalize(hash.data());
        BOOST_CHECK(hash == out);
    }
    for (int i=0; i<32; i++) {
        // Test that writing the string broken up in random pieces works.
        Hasher hasher(h);
        size_t pos = 0;
        while (pos < in.size()) {
            size_t len = InsecureRandRange((in.size() - pos + 1) / 2 + 1);
            hasher.Write((const uint8_t*)in.data() + pos, len);
            pos += len;
            if (pos > 0 && pos + 2 * out.size() > in.size() && pos < in.size()) {
                // Test that writing the rest at once to a copy of a hasher works.
                Hasher(hasher).Write((const uint8_t*)in.data() + pos, in.size() - pos).Finalize(hash.data());
                BOOST_CHECK(hash == out);
            }
        }
        hasher.Finalize(hash.data());
        BOOST_CHECK(hash == out);
    }
}

static void TestSHA1(const std::string &in, const std::string &hexout) { TestVector(CSHA1(), in, ParseHex(hexout));}
static void TestSHA256(const std::string &in, const std::string &hexout) { TestVector(CSHA256(), in, ParseHex(hexout));}
static void TestSHA512(const std::string &in, const std::string &hexout) { TestVector(CSHA512(), in, ParseHex(hexout));}
static void TestRIPEMD160(const std::string &in, const std::string &hexout) { TestVector(CRIPEMD160(), in, ParseHex(hexout));}

static void TestHMACSHA256(const std::string &hexkey, const std::string &hexin, const std::string &hexout) {
    std::vector<unsigned char> key = ParseHex(hexkey);
    TestVector(CHMAC_SHA256(key.data(), key.size()), ParseHex(hexin), ParseHex(hexout));
}

static void TestHMACSHA512(const std::string &hexkey, const std::string &hexin, const std::string &hexout) {
    std::vector<unsigned char> key = ParseHex(hexkey);
    TestVector(CHMAC_SHA512(key.data(), key.size()), ParseHex(hexin), ParseHex(hexout));
}

static void TestAES256(const std::string &hexkey, const std::string &hexin, const std::string &hexout)
{
    std::vector<unsigned char> key = ParseHex(hexkey);
    std::vector<unsigned char> in = ParseHex(hexin);
    std::vector<unsigned char> correctout = ParseHex(hexout);
    std::vector<unsigned char> buf;

    assert(key.size() == 32);
    assert(in.size() == 16);
    assert(correctout.size() == 16);
    AES256Encrypt enc(key.data());
    buf.resize(correctout.size());
    enc.Encrypt(buf.data(), in.data());
    BOOST_CHECK(buf == correctout);
    AES256Decrypt dec(key.data());
    dec.Decrypt(buf.data(), buf.data());
    BOOST_CHECK(buf == in);
}

static void TestAES256CBC(const std::string &hexkey, const std::string &hexiv, bool pad, const std::string &hexin, const std::string &hexout)
{
    std::vector<unsigned char> key = ParseHex(hexkey);
    std::vector<unsigned char> iv = ParseHex(hexiv);
    std::vector<unsigned char> in = ParseHex(hexin);
    std::vector<unsigned char> correctout = ParseHex(hexout);
    std::vector<unsigned char> realout(in.size() + AES_BLOCKSIZE);

    // Encrypt the plaintext and verify that it equals the cipher
    AES256CBCEncrypt enc(key.data(), iv.data(), pad);
    int size = enc.Encrypt(in.data(), in.size(), realout.data());
    realout.resize(size);
    BOOST_CHECK(realout.size() == correctout.size());
    BOOST_CHECK_MESSAGE(realout == correctout, HexStr(realout) + std::string(" != ") + hexout);

    // Decrypt the cipher and verify that it equals the plaintext
    std::vector<unsigned char> decrypted(correctout.size());
    AES256CBCDecrypt dec(key.data(), iv.data(), pad);
    size = dec.Decrypt(correctout.data(), correctout.size(), decrypted.data());
    decrypted.resize(size);
    BOOST_CHECK(decrypted.size() == in.size());
    BOOST_CHECK_MESSAGE(decrypted == in, HexStr(decrypted) + std::string(" != ") + hexin);

    // Encrypt and re-decrypt substrings of the plaintext and verify that they equal each-other
    for(std::vector<unsigned char>::iterator i(in.begin()); i != in.end(); ++i)
    {
        std::vector<unsigned char> sub(i, in.end());
        std::vector<unsigned char> subout(sub.size() + AES_BLOCKSIZE);
        int _size = enc.Encrypt(sub.data(), sub.size(), subout.data());
        if (_size != 0)
        {
            subout.resize(_size);
            std::vector<unsigned char> subdecrypted(subout.size());
            _size = dec.Decrypt(subout.data(), subout.size(), subdecrypted.data());
            subdecrypted.resize(_size);
            BOOST_CHECK(decrypted.size() == in.size());
            BOOST_CHECK_MESSAGE(subdecrypted == sub, HexStr(subdecrypted) + std::string(" != ") + HexStr(sub));
        }
    }
}

static void TestChaCha20(const std::string &hex_message, const std::string &hexkey, uint64_t nonce, uint64_t seek, const std::string& hexout)
{
    std::vector<unsigned char> key = ParseHex(hexkey);
    std::vector<unsigned char> m = ParseHex(hex_message);
    ChaCha20 rng(key.data(), key.size());
    rng.SetIV(nonce);
    rng.Seek(seek);
    std::vector<unsigned char> out = ParseHex(hexout);
    std::vector<unsigned char> outres;
    outres.resize(out.size());
    assert(hex_message.empty() || m.size() == out.size());

    // perform the ChaCha20 round(s), if message is provided it will output the encrypted ciphertext otherwise the keystream
    if (!hex_message.empty()) {
        rng.Crypt(m.data(), outres.data(), outres.size());
    } else {
        rng.Keystream(outres.data(), outres.size());
    }
    BOOST_CHECK(out == outres);
    if (!hex_message.empty()) {
        // Manually XOR with the keystream and compare the output
        rng.SetIV(nonce);
        rng.Seek(seek);
        std::vector<unsigned char> only_keystream(outres.size());
        rng.Keystream(only_keystream.data(), only_keystream.size());
        for (size_t i = 0; i != m.size(); i++) {
            outres[i] = m[i] ^ only_keystream[i];
        }
        BOOST_CHECK(out == outres);
    }
}

static void TestPoly1305(const std::string &hexmessage, const std::string &hexkey, const std::string& hextag)
{
    std::vector<unsigned char> key = ParseHex(hexkey);
    std::vector<unsigned char> m = ParseHex(hexmessage);
    std::vector<unsigned char> tag = ParseHex(hextag);
    std::vector<unsigned char> tagres;
    tagres.resize(POLY1305_TAGLEN);
    poly1305_auth(tagres.data(), m.data(), m.size(), key.data());
    BOOST_CHECK(tag == tagres);
}

static void TestHKDF_SHA256_32(const std::string &ikm_hex, const std::string &salt_hex, const std::string &info_hex, const std::string &