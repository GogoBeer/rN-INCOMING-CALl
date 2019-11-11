// Copyright (c) 2018-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifdef ENABLE_SSE41

#include <stdint.h>
#include <immintrin.h>

#include <crypto/common.h>

namespace sha256d64_sse41 {
namespace {

__m128i inline K(uint32_t x) { return _mm_set1_epi32(x); }

__m128i inline Add(__m128i x, __m128i y) { return _mm_add_epi32(x, y); }
__m128i inline Add(__m128i x, __m128i y, __m128i z) { return Add(Add(x, y), z); }
__m128i inline Add(__m128i x, __m128i y, __m128i z, __m128i w) { return Add(Add(x, y), Add(z, w)); }
__m128i inline Add(__m128i x, __m128i y, __m128i z, __m128i w, __m128i v) { return Add(Add(x, y, z), Add(w, v)); }
__m128i inline Inc(__m128i& x, __m128i y) { x = Add(x, y); return x; }
__m128i inline Inc(__m128i& x, __m128i y, __m128i z) { x = Add(x, y, z); return x; }
__m128i inline Inc(__m128i& x, __m128i y, __m128i z, __m128i w) { x = Add(x, y, z, w); return x; }
__m128i inline Xor(__m128i x, __m128i y) { return _mm_xor_si128(x, y); }
__m128i inline Xor(__m128i x, __m128i y, __m128i z) { return Xor(Xor(x, y), z); }
__m128i inline Or(__m128i x, __m128i y) { return _mm_or_si128(x, y); }
__m128i inline And(__m128i x, __m128i y) { return _mm_and_si128(x, y); }
__m128i inline ShR(__m128i x, int n) { return _mm_srli_epi32(x, n); }
__m128i inline ShL(__m128i x, int n) { return _mm_slli_epi32(x, n); }

__m128i inline Ch(__m128i x, __m128i y, __m128i z) { return Xor(z, And(x, Xor(y, z))); }
__m128i inline Maj(__m128i x, __m128i y, __m128i z) { return Or(And(x, y), And(z, Or(x, y))); }
__m128i inline Sigma0(__m128i x) { return Xor(Or(ShR(x, 2), ShL(x, 30)), Or(ShR(x, 13), ShL(x, 19)), Or(ShR(x, 22), ShL(x, 10))); }
__m128i inline Sigma1(__m128i x) { return Xor(Or(ShR(x, 6), ShL(x, 26)), Or(ShR(x, 11), ShL(x, 21)), Or(ShR(x, 25), ShL(x, 7))); }
__m128i inline sigma0(__m128i x) { return Xor(Or(ShR(x, 7), ShL(x, 25)), Or(ShR(x, 18), ShL(x, 14)), ShR(x, 3)); }
__m128i inline sigma1(__m128i x) { return Xor(Or(ShR(x, 17), ShL(x, 15)), Or(ShR(x, 19), ShL(x, 13)), ShR(x, 10)); }

/** One round of SHA-256. */
void inline __attribute__((always_inline)) Round(__m128i a, __m128i b, __m128i c, __m128i& d, __m128i e, __m128i f, __m128i g, __m128i& h, __m128i k)
{
    __m128i t1 = Add(h, Sigma1(e), Ch(e, f, g), k);
    __m128i t2 = Add(Sigma0(a), Maj(a, b, c));
    d = Add(d, t1);
    h = Add(t1, t2);
}

__m128i inline Read4(const unsigned char* chunk, int offset) {
    __m128i ret = _mm_set_epi32(
        ReadLE32(chunk + 0 + offset),
        ReadLE32(chunk + 64 + offset),
        ReadLE32(chunk + 128 + offset),
        ReadLE32(chunk + 192 + offset)
    );
    return _mm_shuffle_epi8(ret, _mm_set_epi32(0x0C0D0E0FUL, 0x08090A0BUL, 0x04050607UL, 0x00010203UL));
}

void inline Write4(unsigned char* out, int offset, __m128i v) {
    v = _mm_shuffle_epi8(v, _mm_set_epi32(0x0C0D0E0FUL, 0x08090A0BUL, 0x04050607UL, 0x00010203UL));
    WriteLE32(out + 0 + offset, _mm_extract_epi32(v, 3));
    WriteLE32(out + 32 + offset, _mm_extract_epi32(v, 2));
    WriteLE32(out + 64 + offset, _mm_extract_epi32(v, 1));
    WriteLE32(out + 96 + offset, _mm_extract_epi32(v, 0));
}

}

void Transform_4way(unsigned char* out, const unsigned char* in)
{
    // Transform 1
    __m128i a = K(0x6a09e667ul);
    __m128i b = K(0xbb67ae85ul);
    __m128i c = K(0x3c6ef372ul);
    __m128i d = K(0xa54ff53aul);
    __m128i e = K(0x510e527ful);
    __m128i f = K(0x9b05688cul);
    __m128i g = K(0x1f83d9abul);
    __m128i h = K(0x5be0cd19ul);

    __m128i w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;

    Round(a, b, c, d, e, f, g, h, Add(K(0x428a2f98ul), w0 = Read4(in, 0)));
    Round(h, a, b, c, d, e, f, g, Add(K(0x71374491ul), w1 = Read4(in, 4)));
    Round(g, h, a, b, c, d, e, f, Add(K(0xb5c0fbcful), w2 = Read4(in, 8)));
    Round(f, g, h, a, b, c, d, e, Add(K(0xe9b5dba5ul), w3 = Read4(in, 12)));
    Round(e, f, g, h, a, b, c, d, Add(K(0x3956c25bul), w4 = Read4(in, 16)));
    Round(d, e, f, g, h, a, b, c, Add(K(0x59f111f1ul), w5 = Read4(in, 20)));
    Round(c, d, e, f, g, h, a, b, Add(K(0x923f82a4ul), w6 = Read4(in, 24)));
    Round(b, c, d, e, f, g, h, a, Add(K(0xab1c5ed5ul), w7 = Read4(in, 28)));
    Round(a, b, c, d, e, f, g, h, Add(K(0xd807aa98ul), w8 = Read4(in, 32)));
    Round(h, a, b, c, d, e, f, g, Add(K(0x12835b01ul), w9 = Read4(in, 36)));
    Round(g, h, a, b, c, d, e, f, Add(K(0x243185beul), w10 = Read4(in, 40)));
    Round(f, g, h, a, b, c, d, e, Add(K(0x550c7dc3ul), w11 = Read4(in, 44)));
    Round(e, f, g, h, a, b, c, d, Add(K(0x72be5d74ul), w12 = Read4(in, 48)));
    Round(d, e, f, g, h, a, b, c, Add(K(0x80deb1feul), w13 = Read4(in, 52)));
    Round(c, d, e, f, g, h, a, b, Add(K(0x9bdc06a7ul), w14 = Read4(in, 56)));
    Round(b, c, d, e, f, g, h, a, Add(K(0xc19bf174ul), w15 = Read4(in, 60)));
    Round(a, b, c, d, e, f, g, h, Add(K(0xe49b69c1ul), Inc(w0, sigma1(w14), w9, sigma0(w1))));
    Round(h, a, b, c, d, e, f, g, Add(K(0xefbe4786ul), Inc(w1, sigma1(w15), w10, sigma0(w2))));
    Round(g, h, a, b, c, d, e, f, Add(K(0x0fc19dc6ul), Inc(w2, sigma1(w0), w11, sigma0(w3))));
    Round(f, g, h, a, b, c, d, e, Add(K(0x240ca1ccul), Inc(w3, sigma1(w1), w12, sigma0(w4))));
    Round(e, f, g, h, a, b, c, d, Add(K(0x2de92c6ful), Inc(w4, sigma1(w2), w13, sigma0(w5))));
    Round(d, e, f, g, h, a, b, c, Add(K(0x4a7484aaul), Inc(w5, sigma1(w3), w14, sigma0(w6))));
    Round(c, d, e, f, g, h, a, b, Add(K(0x5cb0a9dcul), Inc(w6, sigma1(w4), w15, sigma0(w7))));
    Round(b, c, d, e, f, g, h, a, Add(K(0x76f988daul), Inc(w7, sigma1(w5), w0, sigma0(w8))));
    Round(a, b, c, d, e, f, g, h, Add(K(0x983e5152ul), Inc(w8, sigma1(w6), w1, sigma0(w9))));
    Round(h, a, b, c, d, e, f, g, Add(K(0xa831c66dul), Inc(w9, sigma1(w7), w2, sigma0(w10))));
    Round(g, h, a, b, c, d, e, f, Add(K(0xb00327c8ul), Inc(w10, sigma1(w8), w3, sigma0(w11))));
    Round(f, g, h, a, b, c, d, e, Add(K(0xbf597fc7ul), Inc(w11, sigma1(w9), w4, sigma0(w12))));
    Round(e, f, g, h, a, b, c, d, Add(K(0xc6e00bf3ul), Inc(w12, sigma1(w10), w5, sigma0(w13))));
    Round(d, e, f, g, h, a, b, c, Add(K(0xd5a79147ul), Inc(w13, sigma1(w11), w6, sigma0(w14))));
    Round(c, d, e, f, g, h, a, b, Add(K(0x06ca6351ul), Inc(w14, sigma1(w12), w7, sigma0(w15))));
    Round(b, c, d, e, f, g, h, a, Add(K(0x14292967ul), Inc(w15, sigma1(w13), w8, sigma0(w0))));
    Round(a, b, c, d, e, f, g, h, Add(K(0x27b70a85ul), Inc(w0, sigma1(w14), w9, sigma0(w1))));
    Round(h, a, b, c, d, e, f, g, Add(K(0x2e1b2138ul), Inc(w1, sigma1(w15), w10, sigma0(w2))));
    Round(g, h, a, b, c, d, e, f, Add(K(0x4d2c6dfcul), Inc(w2, sigma1(w0), w11, sigma0(w3))));
    Round(f, g, h, a, b, c, d, e, Add(K(0x53380d13ul), Inc(w3, sigma1(w1), w12, sigma0(w4))));
    Round(e, f, g, h, a, b, c, d, Add(K(0x650a7354ul), Inc(w4, sigma1(w2), w13, sigma0(w5))));
    Round(d, e, f, g, h, a, b, c, Add(K(0x766a0abbul), Inc(w5, sigma1(w3), w14, sigma0(w6))));
    Round(c, d, e, f, g, h, a, b, Add(K(0x81c2c92eul), Inc(w6, sigma1(w4), w15, sigma0(w7))));
    Round(b, c, d, e, f, g, h, a, Add(K(0x92722c85ul), Inc(w7, sigma1(w5), w0, sigma0(w8))));
    Round(a, b, c, d, e, f, g, h, Add(K(0xa2bfe8a1ul), Inc(w8, sigma1(w6), w1, sigma0(w9))));
    Round(h, a, b, c, d, e, f, g, Add(K(0xa81a664bul), Inc(w9, sigma1(w7), w2, sigma0(w10))));
    Round(g, h, a, b, c, d, e, f, Add(K(0xc24b8b70ul), Inc(w10, sigma1(w8), w3, sigma0(w11))));
    Round(f, g, h, a, b, c, d, e, Add(K(0xc76c51a3ul), Inc(w11, sigma1(w9), w4, sigma0(w12))));
    Round(e, f, g, h, a, b, c, d, Add(K(0xd192e819ul), Inc(w12, sigma1(w10), w5, sigma0(w13))));
    Round(d, e, f, g, h, a, b, c, Add(K(0xd6990624ul), Inc(w13, sigma1(w11), w6, sigma0(w14))));
    Round(c, d, e, f, g, h, a, b, Add(K(0xf40e3585ul), Inc