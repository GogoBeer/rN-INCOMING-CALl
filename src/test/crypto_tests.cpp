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

static void TestHKDF_SHA256_32(const std::string &ikm_hex, const std::string &salt_hex, const std::string &info_hex, const std::string &okm_check_hex) {
    std::vector<unsigned char> initial_key_material = ParseHex(ikm_hex);
    std::vector<unsigned char> salt = ParseHex(salt_hex);
    std::vector<unsigned char> info = ParseHex(info_hex);


    // our implementation only supports strings for the "info" and "salt", stringify them
    std::string salt_stringified(reinterpret_cast<char*>(salt.data()), salt.size());
    std::string info_stringified(reinterpret_cast<char*>(info.data()), info.size());

    CHKDF_HMAC_SHA256_L32 hkdf32(initial_key_material.data(), initial_key_material.size(), salt_stringified);
    unsigned char out[32];
    hkdf32.Expand32(info_stringified, out);
    BOOST_CHECK(HexStr(out) == okm_check_hex);
}

static std::string LongTestString()
{
    std::string ret;
    for (int i = 0; i < 200000; i++) {
        ret += (char)(i);
        ret += (char)(i >> 4);
        ret += (char)(i >> 8);
        ret += (char)(i >> 12);
        ret += (char)(i >> 16);
    }
    return ret;
}

const std::string test1 = LongTestString();

BOOST_AUTO_TEST_CASE(ripemd160_testvectors) {
    TestRIPEMD160("", "9c1185a5c5e9fc54612808977ee8f548b2258d31");
    TestRIPEMD160("abc", "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");
    TestRIPEMD160("message digest", "5d0689ef49d2fae572b881b123a85ffa21595f36");
    TestRIPEMD160("secure hash algorithm", "20397528223b6a5f4cbc2808aba0464e645544f9");
    TestRIPEMD160("RIPEMD160 is considered to be safe", "a7d78608c7af8a8e728778e81576870734122b66");
    TestRIPEMD160("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                  "12a053384a9c0c88e405a06c27dcf49ada62eb2b");
    TestRIPEMD160("For this sample, this 63-byte string will be used as input data",
                  "de90dbfee14b63fb5abf27c2ad4a82aaa5f27a11");
    TestRIPEMD160("This is exactly 64 bytes long, not counting the terminating byte",
                  "eda31d51d3a623b81e19eb02e24ff65d27d67b37");
    TestRIPEMD160(std::string(1000000, 'a'), "52783243c1697bdbe16d37f97f68f08325dc1528");
    TestRIPEMD160(test1, "464243587bd146ea835cdf57bdae582f25ec45f1");
}

BOOST_AUTO_TEST_CASE(sha1_testvectors) {
    TestSHA1("", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    TestSHA1("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    TestSHA1("message digest", "c12252ceda8be8994d5fa0290a47231c1d16aae3");
    TestSHA1("secure hash algorithm", "d4d6d2f0ebe317513bbd8d967d89bac5819c2f60");
    TestSHA1("SHA1 is considered to be safe", "f2b6650569ad3a8720348dd6ea6c497dee3a842a");
    TestSHA1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    TestSHA1("For this sample, this 63-byte string will be used as input data",
             "4f0ea5cd0585a23d028abdc1a6684e5a8094dc49");
    TestSHA1("This is exactly 64 bytes long, not counting the terminating byte",
             "fb679f23e7d1ce053313e66e127ab1b444397057");
    TestSHA1(std::string(1000000, 'a'), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    TestSHA1(test1, "b7755760681cbfd971451668f32af5774f4656b5");
}

BOOST_AUTO_TEST_CASE(sha256_testvectors) {
    TestSHA256("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    TestSHA256("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    TestSHA256("message digest",
               "f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650");
    TestSHA256("secure hash algorithm",
               "f30ceb2bb2829e79e4ca9753d35a8ecc00262d164cc077080295381cbd643f0d");
    TestSHA256("SHA256 is considered to be safe",
               "6819d915c73f4d1e77e4e1b52d1fa0f9cf9beaead3939f15874bd988e2a23630");
    TestSHA256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    TestSHA256("For this sample, this 63-byte string will be used as input data",
               "f08a78cbbaee082b052ae0708f32fa1e50c5c421aa772ba5dbb406a2ea6be342");
    TestSHA256("This is exactly 64 bytes long, not counting the terminating byte",
               "ab64eff7e88e2e46165e29f2bce41826bd4c7b3552f6b382a9e7d3af47c245f8");
    TestSHA256("As Bitcoin relies on 80 byte header hashes, we want to have an example for that.",
               "7406e8de7d6e4fffc573daef05aefb8806e7790f55eab5576f31349743cca743");
    TestSHA256(std::string(1000000, 'a'),
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    TestSHA256(test1, "a316d55510b49662420f49d145d42fb83f31ef8dc016aa4e32df049991a91e26");
}

BOOST_AUTO_TEST_CASE(sha512_testvectors) {
    TestSHA512("",
               "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
               "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    TestSHA512("abc",
               "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
               "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    TestSHA512("message digest",
               "107dbf389d9e9f71a3a95f6c055b9251bc5268c2be16d6c13492ea45b0199f33"
               "09e16455ab1e96118e8a905d5597b72038ddb372a89826046de66687bb420e7c");
    TestSHA512("secure hash algorithm",
               "7746d91f3de30c68cec0dd693120a7e8b04d8073cb699bdce1a3f64127bca7a3"
               "d5db502e814bb63c063a7a5043b2df87c61133395f4ad1edca7fcf4b30c3236e");
    TestSHA512("SHA512 is considered to be safe",
               "099e6468d889e1c79092a89ae925a9499b5408e01b66cb5b0a3bd0dfa51a9964"
               "6b4a3901caab1318189f74cd8cf2e941829012f2449df52067d3dd5b978456c2");
    TestSHA512("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
               "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c335"
               "96fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445");
    TestSHA512("For this sample, this 63-byte string will be used as input data",
               "b3de4afbc516d2478fe9b518d063bda6c8dd65fc38402dd81d1eb7364e72fb6e"
               "6663cf6d2771c8f5a6da09601712fb3d2a36c6ffea3e28b0818b05b0a8660766");
    TestSHA512("This is exactly 64 bytes long, not counting the terminating byte",
               "70aefeaa0e7ac4f8fe17532d7185a289bee3b428d950c14fa8b713ca09814a38"
               "7d245870e007a80ad97c369d193e41701aa07f3221d15f0e65a1ff970cedf030");
    TestSHA512("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
               "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
               "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
               "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
    TestSHA512(std::string(1000000, 'a'),
               "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
               "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b");
    TestSHA512(test1,
               "40cac46c147e6131c5193dd5f34e9d8bb4951395f27b08c558c65ff4ba2de594"
               "37de8c3ef5459d76a52cedc02dc499a3c9ed9dedbfb3281afd9653b8a112fafc");
}

BOOST_AUTO_TEST_CASE(hmac_sha256_testvectors) {
    // test cases 1, 2, 3, 4, 6 and 7 of RFC 4231
    TestHMACSHA256("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
                   "4869205468657265",
                   "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    TestHMACSHA256("4a656665",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    TestHMACSHA256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                   "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                   "dddddddddddddddddddddddddddddddddddd",
                   "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    TestHMACSHA256("0102030405060708090a0b0c0d0e0f10111213141516171819",
                   "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
                   "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd",
                   "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    TestHMACSHA256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaa",
                   "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a"
                   "65204b6579202d2048617368204b6579204669727374",
                   "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    TestHMACSHA256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaa",
                   "5468697320697320612074657374207573696e672061206c6172676572207468"
                   "616e20626c6f636b2d73697a65206b657920616e642061206c61726765722074"
                   "68616e20626c6f636b2d73697a6520646174612e20546865206b6579206e6565"
                   "647320746f20626520686173686564206265666f7265206265696e6720757365"
                   "642062792074686520484d414320616c676f726974686d2e",
                   "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
    // Test case with key length 63 bytes.
    TestHMACSHA256("4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a6566",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "9de4b546756c83516720a4ad7fe7bdbeac4298c6fdd82b15f895a6d10b0769a6");
    // Test case with key length 64 bytes.
    TestHMACSHA256("4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "528c609a4c9254c274585334946b7c2661bad8f1fc406b20f6892478d19163dd");
    // Test case with key length 65 bytes.
    TestHMACSHA256("4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "d06af337f359a2330deffb8e3cbe4b5b7aa8ca1f208528cdbd245d5dc63c4483");
}

BOOST_AUTO_TEST_CASE(hmac_sha512_testvectors) {
    // test cases 1, 2, 3, 4, 6 and 7 of RFC 4231
    TestHMACSHA512("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
                   "4869205468657265",
                   "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
                   "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
    TestHMACSHA512("4a656665",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
                   "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
    TestHMACSHA512("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                   "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                   "dddddddddddddddddddddddddddddddddddd",
                   "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
                   "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb");
    TestHMACSHA512("0102030405060708090a0b0c0d0e0f10111213141516171819",
                   "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
                   "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd",
                   "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
                   "a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd");
    TestHMACSHA512("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaa",
                   "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a"
                   "65204b6579202d2048617368204b6579204669727374",
                   "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
                   "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598");
    TestHMACSHA512("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaa",
                   "5468697320697320612074657374207573696e672061206c6172676572207468"
                   "616e20626c6f636b2d73697a65206b657920616e642061206c61726765722074"
                   "68616e20626c6f636b2d73697a6520646174612e20546865206b6579206e6565"
                   "647320746f20626520686173686564206265666f7265206265696e6720757365"
                   "642062792074686520484d414320616c676f726974686d2e",
                   "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
                   "b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58");
    // Test case with key length 127 bytes.
    TestHMACSHA512("4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a6566",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "267424dfb8eeb999f3e5ec39a4fe9fd14c923e6187e0897063e5c9e02b2e624a"
                   "c04413e762977df71a9fb5d562b37f89dfdfb930fce2ed1fa783bbc2a203d80e");
    // Test case with key length 128 bytes.
    TestHMACSHA512("4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "43aaac07bb1dd97c82c04df921f83b16a68d76815cd1a30d3455ad43a3d80484"
                   "2bb35462be42cc2e4b5902de4d204c1c66d93b47d1383e3e13a3788687d61258");
    // Test case with key length 129 bytes.
    TestHMACSHA512("4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a6566654a6566654a6566654a6566654a6566654a6566654a6566654a656665"
                   "4a",
                   "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
                   "0b273325191cfc1b4b71d5075c8fcad67696309d292b1dad2cd23983a35feb8e"
                   "fb29795e79f2ef27f68cb1e16d76178c307a67beaad9456fac5fdffeadb16e2c");
}

BOOST_AUTO_TEST_CASE(aes_testvectors) {
    // AES test vectors from FIPS 197.
    TestAES256("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "00112233445566778899aabbccddeeff", "8ea2b7ca516745bfeafc49904b496089");

    // AES-ECB test vectors from NIST sp800-38a.
    TestAES256("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", "6bc1bee22e409f96e93d7e117393172a", "f3eed1bdb5d2a03c064b5a7e3db181f8");
    TestAES256("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", "ae2d8a571e03ac9c9eb76fac45af8e51", "591ccb10d410ed26dc5ba74a31362870");
    TestAES256("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", "30c81c46a35ce411e5fbc1191a0a52ef", "b6ed21b99ca6f4f9f153e7b1beafed1d");
    TestAES256("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", "f69f2445df4f9b17ad2b417be66c3710", "23304b7a39f9f3ff067d8d8f9e24ecc7");
}

BOOST_AUTO_TEST_CASE(aes_cbc_testvectors) {
    // NIST AES CBC 256-bit encryption test-vectors
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "000102030405060708090A0B0C0D0E0F", false, "6bc1bee22e409f96e93d7e117393172a", \
                  "f58c4c04d6e5f1ba779eabfb5f7bfbd6");
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "F58C4C04D6E5F1BA779EABFB5F7BFBD6", false, "ae2d8a571e03ac9c9eb76fac45af8e51", \
                  "9cfc4e967edb808d679f777bc6702c7d");
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "9CFC4E967EDB808D679F777BC6702C7D", false, "30c81c46a35ce411e5fbc1191a0a52ef",
                  "39f23369a9d9bacfa530e26304231461");
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "39F23369A9D9BACFA530E26304231461", false, "f69f2445df4f9b17ad2b417be66c3710", \
                  "b2eb05e2c39be9fcda6c19078c6a9d1b");

    // The same vectors with padding enabled
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "000102030405060708090A0B0C0D0E0F", true, "6bc1bee22e409f96e93d7e117393172a", \
                  "f58c4c04d6e5f1ba779eabfb5f7bfbd6485a5c81519cf378fa36d42b8547edc0");
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "F58C4C04D6E5F1BA779EABFB5F7BFBD6", true, "ae2d8a571e03ac9c9eb76fac45af8e51", \
                  "9cfc4e967edb808d679f777bc6702c7d3a3aa5e0213db1a9901f9036cf5102d2");
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "9CFC4E967EDB808D679F777BC6702C7D", true, "30c81c46a35ce411e5fbc1191a0a52ef",
                  "39f23369a9d9bacfa530e263042314612f8da707643c90a6f732b3de1d3f5cee");
    TestAES256CBC("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", \
                  "39F23369A9D9BACFA530E26304231461", true, "f69f2445df4f9b17ad2b417be66c3710", \
                  "b2eb05e2c39be9fcda6c19078c6a9d1b3f461796d6b0d6b2e0c2a72b4d80e644");
}


BOOST_AUTO_TEST_CASE(chacha20_testvector)
{
    // Test vector from RFC 7539

    // test encryption
    TestChaCha20("4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756"
                 "c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e"
                 "20776f756c642062652069742e",
                 "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", 0x4a000000UL, 1,
                 "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b3571639d"
                 "624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365af90bbf74"
                 "a35be6b40b8eedf2785e42874d"
                 );

    // test keystream output
    TestChaCha20("", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", 0x4a000000UL, 1,
                 "224f51f3401bd9e12fde276fb8631ded8c131f823d2c06e27e4fcaec9ef3cf788a3b0aa372600a92b57974cded2b9334794cb"
                 "a40c63e34cdea212c4cf07d41b769a6749f3f630f4122cafe28ec4dc47e26d4346d70b98c73f3e9c53ac40c5945398b6eda1a"
                 "832c89c167eacd901d7e2bf363");

    // Test vectors from https://tools.ietf.org/html/draft-agl-tls-chacha20poly1305-04#section-7
    TestChaCha20("", "0000000000000000000000000000000000000000000000000000000000000000", 0, 0,
                 "76b8e0ada0f13d90405d6ae55386bd28bdd219b8a08ded1aa836efcc8b770dc7da41597c5157488d7724e03fb8d84a376a43b"
                 "8f41518a11cc387b669b2ee6586");
    TestChaCha20("", "0000000000000000000000000000000000000000000000000000000000000001", 0, 0,
                 "4540f05a9f1fb296d7736e7b208e3c96eb4fe1834688d2604f450952ed432d41bbe2a0b6ea7566d2a5d1e7e20d42af2c53d79"
                 "2b1c43fea817e9ad275ae546963");
    TestChaCha20("", "0000000000000000000000000000000000000000000000000000000000000000", 0x0100000000000000ULL, 0,
                 "de9cba7bf3d69ef5e786dc63973f653a0b49e015adbff7134fcb7df137821031e85a050278a7084527214f73efc7fa5b52770"
                 "62eb7a0433e445f41e3");
    TestChaCha20("", "0000000000000000000000000000000000000000000000000000000000000000", 1, 0,
                 "ef3fdfd6c61578fbf5cf35bd3dd33b8009631634d21e42ac33960bd138e50d32111e4caf237ee53ca8ad6426194a88545ddc4"
                 "97a0b466e7d6bbdb0041b2f586b");
    TestChaCha20("", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", 0x0706050403020100ULL, 0,
                 "f798a189f195e66982105ffb640bb7757f579da31602fc93ec01ac56f85ac3c134a4547b733b46413042c9440049176905d3b"
                 "e59ea1c53f15916155c2be8241a38008b9a26bc35941e2444177c8ade6689de95264986d95889fb60e84629c9bd9a5acb1cc1"
                 "18be563eb9b3a4a472f82e09a7e778492b562ef7130e88dfe031c79db9d4f7c7a899151b9a475032b63fc385245fe054e3dd5"
                 "a97a5f576fe064025d3ce042c566ab2c507b138db853e3d6959660996546cc9c4a6eafdc777c040d70eaf46f76dad3979e5c5"
                 "360c3317166a1c894c94a371876a94df7628fe4eaaf2ccb27d5aaae0ad7ad0f9d4b6ad3b54098746d4524d38407a6deb3ab78"
                 "fab78c9");
}

BOOST_AUTO_TEST_CASE(poly1305_testvector)
{
    // RFC 7539, section 2.5.2.
    TestPoly1305("43727970746f6772617068696320466f72756d2052657365617263682047726f7570",
                 "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b",
                 "a8061dc1305136c6c22b8baf0c0127a9");

    // RFC 7539, section A.3.
    TestPoly1305("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                 "000000000000000000000000000",
                 "0000000000000000000000000000000000000000000000000000000000000000",
                 "00000000000000000000000000000000");

    TestPoly1305("416e79207375626d697373696f6e20746f20746865204945544620696e74656e6465642062792074686520436f6e747269627"
                 "5746f7220666f72207075626c69636174696f6e20617320616c6c206f722070617274206f6620616e204945544620496e7465"
                 "726e65742d4472616674206f722052464320616e6420616e792073746174656d656e74206d6164652077697468696e2074686"
                 "520636f6e74657874206f6620616e204945544620616374697669747920697320636f6e7369646572656420616e2022494554"
                 "4620436f6e747269627574696f6e222e20537563682073746174656d656e747320696e636c756465206f72616c20737461746"
                 "56d656e747320696e20494554462073657373696f6e732c2061732077656c6c206173207772697474656e20616e6420656c65"
                 "6374726f6e696320636f6d6d756e69636174696f6e73206d61646520617420616e792074696d65206f7220706c6163652c207"
                 "768696368206172652061646472657373656420746f",
                 "0000000000000000000000000000000036e5f6b5c5e06070f0efca96227a863e",
                 "36e5f6b5c5e06070f0efca96227a863e");

    TestPoly1305("416e79207375626d697373696f6e20746f20746865204945544620696e74656e6465642062792074686520436f6e747269627"
                 "5746f7220666f72207075626c69636174696f6e20617320616c6c206f722070617274206f6620616e204945544620496e7465"
                 "726e65742d4472616674206f722052464320616e6420616e792073746174656d656e74206d6164652077697468696e2074686"
                 "520636f6e74657874206f6620616e204945544620616374697669747920697320636f6e7369646572656420616e2022494554"
                 "4620436f6e747269627574696f6e222e20537563682073746174656d656e747320696e636c756465206f72616c20737461746"
                 "56d656e747320696e20494554462073657373696f6e732c2061732077656c6c206173207772697474656e20616e6420656c65"
                 "6374726f6e696320636f6d6d756e69636174696f6e73206d61646520617420616e792074696d65206f7220706c6163652c207"
                 "768696368206172652061646472657373656420746f",
                 "36e5f6b5c5e06070f0efca96227a863e00000000000000000000000000000000",
                 "f3477e7cd95417af89a6b8794c310cf0");

    TestPoly1305("2754776173206272696c6c69672c20616e642074686520736c6974687920746f7665730a446964206779726520616e6420676"
                 "96d626c6520696e2074686520776162653a0a416c6c206d696d737920776572652074686520626f726f676f7665732c0a416e"
                 "6420746865206d6f6d65207261746873206f757467726162652e",
                 "1c9240a5eb55d38af333888604f6b5f0473917c1402b80099dca5cbc207075c0",
                 "4541669a7eaaee61e708dc7cbcc5eb62");

    TestPoly1305("ffffffffffffffffffffffffffffffff",
                 "0200000000000000000000000000000000000000000000000000000000000000",
                 "03000000000000000000000000000000");

    TestPoly1305("02000000000000000000000000000000",
                 "02000000000000000000000000000000ffffffffffffffffffffffffffffffff",
                 "03000000000000000000000000000000");

    TestPoly1305("fffffffffffffffffffffffffffffffff0ffffffffffffffffffffffffffffff11000000000000000000000000000000",
                 "0100000000000000000000000000000000000000000000000000000000000000",
                 "05000000000000000000000000000000");

    TestPoly1305("fffffffffffffffffffffffffffffffffbfefefefefefefefefefefefefefefe01010101010101010101010101010101",
                 "0100000000000000000000000000000000000000000000000000000000000000",
                 "00000000000000000000000000000000");

    TestPoly1305("fdffffffffffffffffffffffffffffff",
                 "0200000000000000000000000000000000000000000000000000000000000000",
                 "faffffffffffffffffffffffffffffff");

    TestPoly1305("e33594d7505e43b900000000000000003394d7505e4379cd01000000000000000000000000000000000000000000000001000000000000000000000000000000",
                 "0100000000000000040000000000000000000000000000000000000000000000",
                 "14000000000000005500000000000000");

    TestPoly1305("e33594d7505e43b900000000000000003394d7505e4379cd010000000000000000000000000000000000000000000000",
                 "0100000000000000040000000000000000000000000000000000000000000000",
                 "13000000000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(hkdf_hmac_sha256_l32_tests)
{
    // Use rfc5869 test vectors but truncated to 32 bytes (our implementation only support length 32)
    TestHKDF_SHA256_32(
                /*ikm_hex=*/"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
                /*salt_hex=*/"000102030405060708090a0b0c",
                /*info_hex=*/"f0f1f2f3f4f5f6f7f8f9",
                /*okm_check_hex=*/"3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf");
    TestHKDF_SHA256_32(
                "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f",
                "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
                "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
                "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c");
    TestHKDF_SHA256_32(
                "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
                "",
                "",
                "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d");
}

static void TestChaCha20Poly1305AEAD(bool must_succeed, unsigned int expected_aad_length, const std::string& hex_m, const std::string& hex_k1, const std::string& hex_k2, const std::string& hex_aad_keystream, const std::string& hex_encrypted_message, const std::string& hex_encrypted_message_seq_999)
{
    // we need two sequence numbers, one for the payload cipher instance...
    uint32_t seqnr_payload = 0;
    // ... and one for the AAD (length) cipher instance
    uint32_t seqnr_aad = 0;
    // we need to keep track of the position in the AAD cipher instance
    // keystream since we use the same 64byte output 21 times
    // (21 times 3 bytes length < 64)
    int aad_pos = 0;

    std::vector<unsigned char> aead_K_1 = ParseHex(hex_k1);
    std::vector<unsigned char> aead_K_2 = ParseHex(hex_k2);
    std::vector<unsigned char> plaintext_buf = ParseHex(hex_m);
    std::vector<unsigned char> expected_aad_keystream = ParseHex(hex_aad_keystream);
    std::vector<unsigned char> expected_ciphertext_and_mac = ParseHex(hex_encrypted_message);
    std::vector<unsigned char> expected_ciphertext_and_mac_sequence999 = ParseHex(hex_encrypted_message_seq_999);

    std::vector<unsigned char> ciphertext_buf(plaintext_buf.size() + POLY1305_TAGLEN, 0);
    std::vector<unsigned char> plaintext_buf_new(plaintext_buf.size(), 0);
    std::vector<unsigned char> cmp_ctx_buffer(64);
    uint32_t out_len = 0;

    // create the AEAD instance
    ChaCha20Poly1305AEAD aead(aead_K_1.data(), aead_K_1.size(), aead_K_2.data(), aead_K_2.size());

    // create a chacha20 instance to compare against
    ChaCha20 cmp_ctx(aead_K_1.data(), 32);

    // encipher
    bool res = aead.Crypt(seqnr_payload, seqnr_aad, aad_pos, ciphertext_buf.data(), ciphertext_buf.size(), plaintext_buf.data(), plaintext_buf.size(), true);
    // make sure the operation succeeded if expected to succeed
    BOOST_CHECK_EQUAL(res, must_succeed);
    if (!res) return;

    // verify ciphertext & mac against the test vector
    BOOST_CHECK_EQUAL(expected_ciphertext_and_mac.size(), ciphertext_buf.size());
    BOOST_CHECK(memcmp(ciphertext_buf.data(), expected_ciphertext_and_mac.data(), ciphertext_buf.size()) == 0);

    // manually construct the AAD keystream
    cmp_ctx.SetIV(seqnr_aad);
    cmp_ctx.Seek(0);
    cmp_ctx.Keystream(cmp_ctx_buffer.data(), 64);
    BOOST_CHECK(memcmp(expected_aad_keystream.data(), cmp_ctx_buffer.data(), expected_aad_keystream.size()) == 0);
    // crypt the 3 length bytes and compare the length
    uint32_t len_cmp = 0;
    len_cmp = (ciphertext_buf[0] ^ cmp_ctx_buffer[aad_pos + 0]) |
              (ciphertext_buf[1] ^ cmp_ctx_buffer[aad_pos + 1]) << 8 |
              (ciphertext_buf[2] ^ cmp_ctx_buffer[aad_pos + 2]) << 16;
    BOOST_CHECK_EQUAL(len_cmp, expected_aad_length);

    // encrypt / decrypt 1000 packets
    for (size_t i = 0; i < 1000; ++i) {
        res = aead.Crypt(seqnr_payload, seqnr_aad, aad_pos, ciphertext_buf.data(), ciphertext_buf.size(), plaintext_buf.data(), plaintext_buf.size(), true);
        BOOST_CHECK(res);
        BOOST_CHECK(aead.GetLength(&out_len, seqnr_aad, aad_pos, ciphertext_buf.data()));
        BOOST_CHECK_EQUAL(out_len, expected_aad_length);
        res = aead.Crypt(seqnr_payload, seqnr_aad, aad_pos, plaintext_buf_new.data(), plaintext_buf_new.size(), ciphertext_buf.data(), ciphertext_buf.size(), false);
        BOOST_CHECK(res);

        // make sure we repetitive get the same plaintext
        BOOST_CHECK(memcmp(plaintext_buf.data(), plaintext_buf_new.data(), plaintext_buf.size()) == 0);

        // compare sequence number 999 against the test vector
        if (seqnr_payload == 999) {
            BOOST_CHECK(memcmp(ciphertext_buf.data(), expected_ciphertext_and_mac_sequence999.data(), expected_ciphertext_and_mac_sequence999.size()) == 0);
        }
        // set nonce and block counter, output the keystream
        cmp_ctx.SetIV(seqnr_aad);
        cmp_ctx.Seek(0);
        cmp_ctx.Keystream(cmp_ctx_buffer.data(), 64);

        // crypt the 3 length bytes and compare the length
        len_cmp = 0;
        len_cmp = (ciphertext_buf[0] ^ cmp_ctx_buffer[aad_pos + 0]) |
                  (ciphertext_buf[1] ^ cmp_ctx_buffer[aad_pos + 1]) << 8 |
                  (ciphertext_buf[2] ^ cmp_ctx_buffer[aad_pos + 2]) << 16;
        BOOST_CHECK_EQUAL(len_cmp, expected_aad_length);

        // increment the sequence number(s)
        // always increment the payload sequence number
        // increment the AAD keystream position by its size (3)
        // increment the AAD sequence number if we would hit the 64 byte limit
        seqnr_payload++;
        aad_pos += CHACHA20_POLY1305_AEAD_AAD_LEN;
        if (aad_pos + CHACHA20_POLY1305_AEAD_AAD_LEN > CHACHA20_ROUND_OUTPUT) {
            aad_pos = 0;
            seqnr_aad++;
        }
    }
}

BOOST_AUTO_TEST_CASE(chacha20_poly1305_aead_testvector)
{
    /* test chacha20poly1305@bitcoin AEAD */

    // must fail with no message
    TestChaCha20Poly1305AEAD(false, 0,
        "",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000", "", "", "");

    TestChaCha20Poly1305AEAD(true, 0,
        /* m  */ "0000000000000000000000000000000000000000000000000000000000000000",
        /* k1 (AAD) */ "0000000000000000000000000000000000000000000000000000000000000000",
        /* k2 (payload) */ "0000000000000000000000000000000000000000000000000000000000000000",
        /* AAD keystream */ "76b8e0ada0f13d90405d6ae55386bd28bdd219b8a08ded1aa836efcc8b770dc7da41597c5157488d7724e03fb8d84a376a43b8f41518a11cc387b669b2ee6586",
        /* encrypted message & MAC */ "76b8e09f07e7be5551387a98ba977c732d080dcb0f29a048e3656912c6533e32d2fc11829c1b6c1df1f551cd6131ff08",
        /* encrypted message & MAC at sequence 999 */ "b0a03d5bd2855d60699e7d3a3133fa47be740fe4e4c1f967555e2d9271f31c3aaa7aa16ec62c5e24f040c08bb20c3598");
    TestChaCha20Poly1305AEAD(true, 1,
        "0100000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "76b8e0ada0f13d90405d6ae55386bd28bdd219b8a08ded1aa836efcc8b770dc7da41597c5157488d7724e03fb8d84a376a43b8f41518a11cc387b669b2ee6586",
        "77b8e09f07e7be5551387a98ba977c732d080dcb0f29a048e3656912c6533e32baf0c85b6dff8602b06cf52a6aefc62e",
        "b1a03d5bd2855d60699e7d3a3133fa47be740fe4e4c1f967555e2d9271f31c3a8bd94d54b5ecabbc41ffbb0c90924080");
    TestChaCha20Poly1305AEAD(true, 255,
        "ff0000f195e66982105ffb640bb7757f579da31602fc93ec01ac56f85ac3c134a4547b733b46413042c9440049176905d3be59ea1c53f15916155c2be8241a38008b9a26bc35941e2444177c8ade6689de95264986d95889fb60e84629c9bd9a5acb1cc118be563eb9b3a4a472f82e09a7e778492b562ef7130e88dfe031c79db9d4f7c7a899151b9a475032b63fc385245fe054e3dd5a97a5f576fe064025d3ce042c566ab2c507b138db853e3d6959660996546cc9c4a6eafdc777c040d70eaf46f76dad3979e5c5360c3317166a1c894c94a371876a94df7628fe4eaaf2ccb27d5aaae0ad7ad0f9d4b6ad3b54098746d4524d38407a6deb3ab78fab78c9",
        "ff0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
        "c640c1711e3ee904ac35c57ab9791c8a1c408603a90b77a83b54f6c844cb4b06d94e7fc6c800e165acd66147e80ec45a567f6ce66d05ec0cae679dceeb890017",
        "3940c1e92da4582ff6f92a776aeb14d014d384eeb30f660dacf70a14a23fd31e91212701334e2ce1acf5199dc84f4d61ddbe6571bca5af874b4c9226c26e650995d157644e1848b96ed6c2102d5489a050e71d29a5a66ece11de5fb5c9558d54da28fe45b0bc4db4e5b88030bfc4a352b4b7068eccf656bae7ad6a35615315fc7c49d4200388d5eca67c2e822e069336c69b40db67e0f3c81209c50f3216a4b89fb3ae1b984b7851a2ec6f68ab12b101ab120e1ea7313bb93b5a0f71185c7fea017ddb92769861c29dba4fbc432280d5dff21b36d1c4c790128b22699950bb18bf74c448cdfe547d8ed4f657d8005fdc0cd7a050c2d46050a44c4376355858981fbe8b184288276e7a93eabc899c4a",
        "f039c6689eaeef0456685200feaab9d54bbd9acde4410a3b6f4321296f4a8ca2604b49727d8892c57e005d799b2a38e85e809f20146e08eec75169691c8d4f54a0d51a1e1c7b381e0474eb02f994be9415ef3ffcbd2343f0601e1f3b172a1d494f838824e4df570f8e3b0c04e27966e36c82abd352d07054ef7bd36b84c63f9369afe7ed79b94f953873006b920c3fa251a771de1b63da927058ade119aa898b8c97e42a606b2f6df1e2d957c22f7593c1e2002f4252f4c9ae4bf773499e5cfcfe14dfc1ede26508953f88553bf4a76a802f6a0068d59295b01503fd9a600067624203e880fdf53933b96e1f4d9eb3f4e363dd8165a278ff667a41ee42b9892b077cefff92b93441f7be74cf10e6cd");
}

BOOST_AUTO_TEST_CASE(countbits_tests)
{
    FastRandomContext ctx;
    for (unsigned int i = 0; i <= 64; ++i) {
        if (i == 0) {
            // Check handling of zero.
            BOOST_CHECK_EQUAL(CountBits(0), 0U);
        } else if (i < 10) {
            for (uint64_t j = (uint64_t)1 << (i - 1); (j >> i) == 0; ++j) {
                // Exhaustively test up to 10 bits
                BOOST_CHECK_EQUAL(CountBits(j), i);
            }
        } else {
            for (int k = 0; k < 1000; k++) {
                // Randomly test 1000 samples of each length above 10 bits.
                uint64_t j = ((uint64_t)1) << (i - 1) | ctx.randbits(i - 1);
                BOOST_CHECK_EQUAL(CountBits(j), i);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(sha256d64)
{
    for (int i = 0; i <= 32; ++i) {
        unsigned char in[64 * 32];
        unsigned char out1[32 * 32], out2[32 * 32];
        for (int j = 0; j < 64 * i; ++j) {
            in[j] = InsecureRandBits(8);
        }
        for (int j = 0; j < i; ++j) {
            CHash256().Write({in + 64 * j, 64}).Finalize({out1 + 32 * j, 32});
        }
        SHA256D64(out2, in, i);
        BOOST_CHECK(memcmp(out1, out2, 32 * i) == 0);
    }
}

static void TestSHA3_256(const std::string& input, const std::string& output)
{
    const auto in_bytes = ParseHex(input);
    const auto out_bytes = ParseHex(output);

    SHA3_256 sha;
    // Hash the whole thing.
    unsigned char out[SHA3_256::OUTPUT_SIZE];
    sha.Write(in_bytes).Finalize(out);
    assert(out_bytes.size() == sizeof(out));
    BOOST_CHECK(std::equal(std::begin(out_bytes), std::end(out_bytes), out));

    // Reset and split randomly in 3
    sha.Reset();
    int s1 = InsecureRandRange(in_bytes.size() + 1);
    int s2 = InsecureRandRange(in_bytes.size() + 1 - s1);
    int s3 = in_bytes.size() - s1 - s2;
    sha.Write(Span{in_bytes}.first(s1)).Write(Span{in_bytes}.subspan(s1, s2));
    sha.Write(Span{in_bytes}.last(s3)).Finalize(out);
    BOOST_CHECK(std::equal(std::begin(out_bytes), std::end(out_bytes), out));
}

BOOST_AUTO_TEST_CASE(keccak_tests)
{
    // Start with the zero state.
    uint64_t state[25] = {0};
    CSHA256 tester;
    for (int i = 0; i < 262144; ++i) {
        KeccakF(state);
        for (int j = 0; j < 25; ++j) {
            unsigned char buf[8];
            WriteLE64(buf, state[j]);
            tester.Write(buf, 8);
        }
    }
    uint256 out;
    tester.Finalize(out.begin());
    // Expected hash of the concatenated serialized states after 1...262144 iterations of KeccakF.
    // Verified against an independent implementation.
    BOOST_CHECK_EQUAL(out.ToString(), "5f4a7f2eca7d57740ef9f1a077b4fc67328092ec62620447fe27ad8ed5f7e34f");
}

BOOST_AUTO_TEST_CASE(sha3_256_tests)
{
    // Test vectors from https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/sha3/sha-3bytetestvectors.zip

    // SHA3-256 Short test vectors (SHA3_256ShortMsg.rsp)
    TestSHA3_256("", "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
    TestSHA3_256("e9", "f0d04dd1e6cfc29a4460d521796852f25d9ef8d28b44ee91ff5b759d72c1e6d6");
    TestSHA3_256("d477", "94279e8f5ccdf6e17f292b59698ab4e614dfe696a46c46da78305fc6a3146ab7");
    TestSHA3_256("b053fa", "9d0ff086cd0ec06a682c51c094dc73abdc492004292344bd41b82a60498ccfdb");
    TestSHA3_256("e7372105", "3a42b68ab079f28c4ca3c752296f279006c4fe78b1eb79d989777f051e4046ae");
    TestSHA3_256("0296f2c40a", "53a018937221081d09ed0497377e32a1fa724025dfdc1871fa503d545df4b40d");
    TestSHA3_256("e6fd42037f80", "2294f8d3834f24aa9037c431f8c233a66a57b23fa3de10530bbb6911f6e1850f");
    TestSHA3_256("37b442385e0538", "cfa55031e716bbd7a83f2157513099e229a88891bb899d9ccd317191819998f8");
    TestSHA3_256("8bca931c8a132d2f", "dbb8be5dec1d715bd117b24566dc3f24f2cc0c799795d0638d9537481ef1e03e");
    TestSHA3_256("fb8dfa3a132f9813ac", "fd09b3501888445ffc8c3bb95d106440ceee469415fce1474743273094306e2e");
    TestSHA3_256("71fbacdbf8541779c24a", "cc4e5a216b01f987f24ab9cad5eb196e89d32ed4aac85acb727e18e40ceef00e");
    TestSHA3_256("7e8f1fd1882e4a7c49e674", "79bef78c78aa71e11a3375394c2562037cd0f82a033b48a6cc932cc43358fd9e");
    TestSHA3_256("5c56a6b18c39e66e1b7a993a", "b697556cb30d6df448ee38b973cb6942559de4c2567b1556240188c55ec0841c");
    TestSHA3_256("9c76ca5b6f8d1212d8e6896ad8", "69dfc3a25865f3535f18b4a7bd9c0c69d78455f1fc1f4bf4e29fc82bf32818ec");
    TestSHA3_256("687ff7485b7eb51fe208f6ff9a1b", "fe7e68ae3e1a91944e4d1d2146d9360e5333c099a256f3711edc372bc6eeb226");
    TestSHA3_256("4149f41be1d265e668c536b85dde41", "229a7702448c640f55dafed08a52aa0b1139657ba9fc4c5eb8587e174ecd9b92");
    TestSHA3_256("d83c721ee51b060c5a41438a8221e040", "b87d9e4722edd3918729ded9a6d03af8256998ee088a1ae662ef4bcaff142a96");
    TestSHA3_256("266e8cbd3e73d80df2a49cfdaf0dc39cd1", "6c2de3c95900a1bcec6bd4ca780056af4acf3aa36ee640474b6e870187f59361");
    TestSHA3_256("a1d7ce5104eb25d6131bb8f66e1fb13f3523", "ee9062f39720b821b88be5e64621d7e0ca026a9fe7248d78150b14bdbaa40bed");
    TestSHA3_256("d751ccd2cd65f27db539176920a70057a08a6b", "7aaca80dbeb8dc3677d18b84795985463650d72f2543e0ec709c9e70b8cd7b79");
    TestSHA3_256("b32dec58865ab74614ea982efb93c08d9acb1bb0", "6a12e535dbfddab6d374058d92338e760b1a211451a6c09be9b61ee22f3bb467");
    TestSHA3_256("4e0cc4f5c6dcf0e2efca1f9f129372e2dcbca57ea6", "d2b7717864e9438dd02a4f8bb0203b77e2d3cd8f8ffcf9dc684e63de5ef39f0d");
    TestSHA3_256("d16d978dfbaecf2c8a04090f6eebdb421a5a711137a6", "7f497913318defdc60c924b3704b65ada7ca3ba203f23fb918c6fb03d4b0c0da");
    TestSHA3_256("47249c7cb85d8f0242ab240efd164b9c8b0bd3104bba3b", "435e276f06ae73aa5d5d6018f58e0f009be351eada47b677c2f7c06455f384e7");
    TestSHA3_256("cf549a383c0ac31eae870c40867eeb94fa1b6f3cac4473f2", "cdfd1afa793e48fd0ee5b34dfc53fbcee43e9d2ac21515e4746475453ab3831f");
    TestSHA3_256("9b3fdf8d448680840d6284f2997d3af55ffd85f6f4b33d7f8d", "25005d10e84ff97c74a589013be42fb37f68db64bdfc7626efc0dd628077493a");
    TestSHA3_256("6b22fe94be2d0b2528d9847e127eb6c7d6967e7ec8b9660e77cc", "157a52b0477639b3bc179667b35c1cdfbb3eef845e4486f0f84a526e940b518c");
    TestSHA3_256("d8decafdad377904a2789551135e782e302aed8450a42cfb89600c", "3ddecf5bba51643cd77ebde2141c8545f862067b209990d4cb65bfa65f4fa0c0");
    TestSHA3_256("938fe6afdbf14d1229e03576e532f078898769e20620ae2164f5abfa", "9511abd13c756772b852114578ef9b96f9dc7d0f2b8dcde6ea7d1bd14c518890");
    TestSHA3_256("66eb5e7396f5b451a02f39699da4dbc50538fb10678ec39a5e28baa3c0", "540acf81810a199996a612e885781308802fe460e9c638cc022e17076be8597a");
    TestSHA3_256("de98968c8bd9408bd562ac6efbca2b10f5769aacaa01365763e1b2ce8048", "6b2f2547781449d4fa158180a178ef68d7056121bf8a2f2f49891afc24978521");
    TestSHA3_256("94464e8fafd82f630e6aab9aa339d981db0a372dc5c1efb177305995ae2dc0", "ea7952ad759653cd47a18004ac2dbb9cf4a1e7bba8a530cf070570c711a634ea");
    TestSHA3_256("c178ce0f720a6d73c6cf1caa905ee724d5ba941c2e2628136e3aad7d853733ba", "64537b87892835ff0963ef9ad5145ab4cfce5d303a0cb0415b3b03f9d16e7d6b");
    TestSHA3_256("14365d3301150d7c5ba6bb8c1fc26e9dab218fc5d01c9ed528b72482aadee9c27bef667907797d55514468f68791f053daa2df598d7db7d54beea493bdcbb0c75c7b36ad84b9996dca96354190bd96d9d7fbe8ff54ffaf77c55eb92985da50825ee3b4179f5ec88b6fa60bb361d0caf9493494fe4d28ef843f0f498a2a9331b82a", "9b690531dee948a9c559a2e0efab2ec824151a9175f2730a030b748d07cbaa7f");
    TestSHA3_256("4a757db93f6d4c6529211d70d5f8491799c0f73ae7f24bbd2138db2eaf2c63a85063b9f7adaa03fc348f275323248334e3ffdf9798859f9cf6693d29566ff7d50976c505ecb58e543c459b39acdf4ce4b5e80a682eaa7c1f1ce5fe4acb864ff91eb6892b23165735ea49626898b40ceeb78161f5d0ea4a103cb404d937f9d1dc362b", "1ac7cc7e2e8ea14fb1b90096f41265100712c5dd41519d78b2786cfb6355af72");
    TestSHA3_256("da11c39c77250f6264dda4b096341ff9c4cc2c900633b20ea1664bf32193f790a923112488f882450cf334819bbaca46ffb88eff0265aa803bc79ca42739e4347c6bff0bb9aa99780261ffe42be0d3b5135d03723338fb2776841a0b4bc26360f9ef769b34c2bec5ed2feb216e2fa30fa5c37430c0360ecbfba3af6fb6b8dedacbb95c", "c163cd43de224ac5c262ae39db746cfcad66074ebaec4a6da23d86b310520f21");
    TestSHA3_256("3341ca020d4835838b0d6c8f93aaaebb7af60730d208c85283f6369f1ee27fd96d38f2674f316ef9c29c1b6b42dd59ec5236f65f5845a401adceaa4cf5bbd91cac61c21102052634e99faedd6cdddcd4426b42b6a372f29a5a5f35f51ce580bb1845a3c7cfcd447d269e8caeb9b320bb731f53fe5c969a65b12f40603a685afed86bfe53", "6c3e93f2b49f493344cc3eb1e9454f79363032beee2f7ea65b3d994b5cae438f");
    TestSHA3_256("989fc49594afc73405bacee4dbbe7135804f800368de39e2ea3bbec04e59c6c52752927ee3aa233ba0d8aab5410240f4c109d770c8c570777c928fce9a0bec9bc5156c821e204f0f14a9ab547e0319d3e758ae9e28eb2dbc3d9f7acf51bd52f41bf23aeb6d97b5780a35ba08b94965989744edd3b1d6d67ad26c68099af85f98d0f0e4fff9", "b10adeb6a9395a48788931d45a7b4e4f69300a76d8b716c40c614c3113a0f051");
    TestSHA3_256("e5022f4c7dfe2dbd207105e2f27aaedd5a765c27c0bc60de958b49609440501848ccf398cf66dfe8dd7d131e04f1432f32827a057b8904d218e68ba3b0398038d755bd13d5f168cfa8a11ab34c0540873940c2a62eace3552dcd6953c683fdb29983d4e417078f1988c560c9521e6f8c78997c32618fc510db282a985f868f2d973f82351d11", "3293a4b9aeb8a65e1014d3847500ffc8241594e9c4564cbd7ce978bfa50767fe");
    TestSHA3_256("b1f6076509938432145bb15dbe1a7b2e007934be5f753908b50fd24333455970a7429f2ffbd28bd6fe1804c4688311f318fe3fcd9f6744410243e115bcb00d7e039a4fee4c326c2d119c42abd2e8f4155a44472643704cc0bc72403b8a8ab0fd4d68e04a059d6e5ed45033b906326abb4eb4147052779bad6a03b55ca5bd8b140e131bed2dfada", "f82d9602b231d332d902cb6436b15aef89acc591cb8626233ced20c0a6e80d7a");
    TestSHA3_256("56ea14d7fcb0db748ff649aaa5d0afdc2357528a9aad6076d73b2805b53d89e73681abfad26bee6c0f3d20215295f354f538ae80990d2281be6de0f6919aa9eb048c26b524f4d91ca87b54c0c54aa9b54ad02171e8bf31e8d158a9f586e92ffce994ecce9a5185cc80364d50a6f7b94849a914242fcb73f33a86ecc83c3403630d20650ddb8cd9c4", "4beae3515ba35ec8cbd1d94567e22b0d7809c466abfbafe9610349597ba15b45");

    // SHA3-256 Long test vectors (SHA3_256LongMsg.rsp)
    TestSHA3_256("b1caa396771a09a1db9bc20543e988e359d47c2a616417bbca1b62cb02796a888fc6eeff5c0b5c3d5062fcb4256f6ae1782f492c1cf03610b4a1fb7b814c057878e1190b9835425c7a4a0e182ad1f91535ed2a35033a5d8c670e21c575ff43c194a58a82d4a1a44881dd61f9f8161fc6b998860cbe4975780be93b6f87980bad0a99aa2cb7556b478ca35d1f3746c33e2bb7c47af426641cc7bbb3425e2144820345e1d0ea5b7da2c3236a52906acdc3b4d34e474dd714c0c40bf006a3a1d889a632983814bbc4a14fe5f159aa89249e7c738b3b73666bac2a615a83fd21ae0a1ce7352ade7b278b587158fd2fabb217aa1fe31d0bda53272045598015a8ae4d8cec226fefa58daa05500906c4d85e7567", "cb5648a1d61c6c5bdacd96f81c9591debc3950dcf658145b8d996570ba881a05");
    TestSHA3_256("712b03d9ebe78d3a032a612939c518a6166ca9a161183a7596aa35b294d19d1f