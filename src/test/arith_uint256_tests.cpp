
// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(arith_uint256_tests)

/// Convert vector to arith_uint256, via uint256 blob
static inline arith_uint256 arith_uint256V(const std::vector<unsigned char>& vch)
{
    return UintToArith256(uint256(vch));
}

const unsigned char R1Array[] =
    "\x9c\x52\x4a\xdb\xcf\x56\x11\x12\x2b\x29\x12\x5e\x5d\x35\xd2\xd2"
    "\x22\x81\xaa\xb5\x33\xf0\x08\x32\xd5\x56\xb1\xf9\xea\xe5\x1d\x7d";
const char R1ArrayHex[] = "7D1DE5EAF9B156D53208F033B5AA8122D2d2355d5e12292b121156cfdb4a529c";
const double R1Ldouble = 0.4887374590559308955; // R1L equals roughly R1Ldouble * 2^256
const arith_uint256 R1L = arith_uint256V(std::vector<unsigned char>(R1Array,R1Array+32));
const uint64_t R1LLow64 = 0x121156cfdb4a529cULL;

const unsigned char R2Array[] =
    "\x70\x32\x1d\x7c\x47\xa5\x6b\x40\x26\x7e\x0a\xc3\xa6\x9c\xb6\xbf"
    "\x13\x30\x47\xa3\x19\x2d\xda\x71\x49\x13\x72\xf0\xb4\xca\x81\xd7";
const arith_uint256 R2L = arith_uint256V(std::vector<unsigned char>(R2Array,R2Array+32));

const char R1LplusR2L[] = "549FB09FEA236A1EA3E31D4D58F1B1369288D204211CA751527CFC175767850C";

const unsigned char ZeroArray[] =
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const arith_uint256 ZeroL = arith_uint256V(std::vector<unsigned char>(ZeroArray,ZeroArray+32));

const unsigned char OneArray[] =
    "\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const arith_uint256 OneL = arith_uint256V(std::vector<unsigned char>(OneArray,OneArray+32));

const unsigned char MaxArray[] =
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
const arith_uint256 MaxL = arith_uint256V(std::vector<unsigned char>(MaxArray,MaxArray+32));

const arith_uint256 HalfL = (OneL << 255);
static std::string ArrayToString(const unsigned char A[], unsigned int width)
{
    std::stringstream Stream;
    Stream << std::hex;
    for (unsigned int i = 0; i < width; ++i)
    {
        Stream<<std::setw(2)<<std::setfill('0')<<(unsigned int)A[width-i-1];
    }
    return Stream.str();
}

BOOST_AUTO_TEST_CASE( basics ) // constructors, equality, inequality
{
    BOOST_CHECK(1 == 0+1);
    // constructor arith_uint256(vector<char>):
    BOOST_CHECK(R1L.ToString() == ArrayToString(R1Array,32));
    BOOST_CHECK(R2L.ToString() == ArrayToString(R2Array,32));
    BOOST_CHECK(ZeroL.ToString() == ArrayToString(ZeroArray,32));
    BOOST_CHECK(OneL.ToString() == ArrayToString(OneArray,32));
    BOOST_CHECK(MaxL.ToString() == ArrayToString(MaxArray,32));
    BOOST_CHECK(OneL.ToString() != ArrayToString(ZeroArray,32));

    // == and !=
    BOOST_CHECK(R1L != R2L);
    BOOST_CHECK(ZeroL != OneL);
    BOOST_CHECK(OneL != ZeroL);
    BOOST_CHECK(MaxL != ZeroL);
    BOOST_CHECK(~MaxL == ZeroL);
    BOOST_CHECK( ((R1L ^ R2L) ^ R1L) == R2L);

    uint64_t Tmp64 = 0xc4dab720d9c7acaaULL;
    for (unsigned int i = 0; i < 256; ++i)
    {
        BOOST_CHECK(ZeroL != (OneL << i));
        BOOST_CHECK((OneL << i) != ZeroL);
        BOOST_CHECK(R1L != (R1L ^ (OneL << i)));
        BOOST_CHECK(((arith_uint256(Tmp64) ^ (OneL << i) ) != Tmp64 ));
    }
    BOOST_CHECK(ZeroL == (OneL << 256));

    // String Constructor and Copy Constructor
    BOOST_CHECK(arith_uint256("0x"+R1L.ToString()) == R1L);
    BOOST_CHECK(arith_uint256("0x"+R2L.ToString()) == R2L);
    BOOST_CHECK(arith_uint256("0x"+ZeroL.ToString()) == ZeroL);
    BOOST_CHECK(arith_uint256("0x"+OneL.ToString()) == OneL);
    BOOST_CHECK(arith_uint256("0x"+MaxL.ToString()) == MaxL);
    BOOST_CHECK(arith_uint256(R1L.ToString()) == R1L);
    BOOST_CHECK(arith_uint256("   0x"+R1L.ToString()+"   ") == R1L);
    BOOST_CHECK(arith_uint256("") == ZeroL);
    BOOST_CHECK(R1L == arith_uint256(R1ArrayHex));
    BOOST_CHECK(arith_uint256(R1L) == R1L);
    BOOST_CHECK((arith_uint256(R1L^R2L)^R2L) == R1L);
    BOOST_CHECK(arith_uint256(ZeroL) == ZeroL);
    BOOST_CHECK(arith_uint256(OneL) == OneL);

    // uint64_t constructor
    BOOST_CHECK( (R1L & arith_uint256("0xffffffffffffffff")) == arith_uint256(R1LLow64));
    BOOST_CHECK(ZeroL == arith_uint256(0));
    BOOST_CHECK(OneL == arith_uint256(1));
    BOOST_CHECK(arith_uint256("0xffffffffffffffff") == arith_uint256(0xffffffffffffffffULL));

    // Assignment (from base_uint)
    arith_uint256 tmpL = ~ZeroL; BOOST_CHECK(tmpL == ~ZeroL);
    tmpL = ~OneL; BOOST_CHECK(tmpL == ~OneL);
    tmpL = ~R1L; BOOST_CHECK(tmpL == ~R1L);
    tmpL = ~R2L; BOOST_CHECK(tmpL == ~R2L);
    tmpL = ~MaxL; BOOST_CHECK(tmpL == ~MaxL);
}

static void shiftArrayRight(unsigned char* to, const unsigned char* from, unsigned int arrayLength, unsigned int bitsToShift)
{
    for (unsigned int T=0; T < arrayLength; ++T)
    {
        unsigned int F = (T+bitsToShift/8);
        if (F < arrayLength)
            to[T]  = from[F] >> (bitsToShift%8);
        else
            to[T] = 0;
        if (F + 1 < arrayLength)
            to[T] |= from[(F+1)] << (8-bitsToShift%8);
    }
}

static void shiftArrayLeft(unsigned char* to, const unsigned char* from, unsigned int arrayLength, unsigned int bitsToShift)
{
    for (unsigned int T=0; T < arrayLength; ++T)
    {
        if (T >= bitsToShift/8)
        {
            unsigned int F = T-bitsToShift/8;
            to[T]  = from[F] << (bitsToShift%8);
            if (T >= bitsToShift/8+1)
                to[T] |= from[F-1] >> (8-bitsToShift%8);
        }
        else {
            to[T] = 0;
        }
    }
}

BOOST_AUTO_TEST_CASE( shifts ) { // "<<"  ">>"  "<<="  ">>="
    unsigned char TmpArray[32];
    arith_uint256 TmpL;
    for (unsigned int i = 0; i < 256; ++i)
    {
        shiftArrayLeft(TmpArray, OneArray, 32, i);
        BOOST_CHECK(arith_uint256V(std::vector<unsigned char>(TmpArray,TmpArray+32)) == (OneL << i));
        TmpL = OneL; TmpL <<= i;
        BOOST_CHECK(TmpL == (OneL << i));
        BOOST_CHECK((HalfL >> (255-i)) == (OneL << i));
        TmpL = HalfL; TmpL >>= (255-i);
        BOOST_CHECK(TmpL == (OneL << i));

        shiftArrayLeft(TmpArray, R1Array, 32, i);
        BOOST_CHECK(arith_uint256V(std::vector<unsigned char>(TmpArray,TmpArray+32)) == (R1L << i));
        TmpL = R1L; TmpL <<= i;
        BOOST_CHECK(TmpL == (R1L << i));

        shiftArrayRight(TmpArray, R1Array, 32, i);
        BOOST_CHECK(arith_uint256V(std::vector<unsigned char>(TmpArray,TmpArray+32)) == (R1L >> i));
        TmpL = R1L; TmpL >>= i;
        BOOST_CHECK(TmpL == (R1L >> i));

        shiftArrayLeft(TmpArray, MaxArray, 32, i);
        BOOST_CHECK(arith_uint256V(std::vector<unsigned char>(TmpArray,TmpArray+32)) == (MaxL << i));
        TmpL = MaxL; TmpL <<= i;
        BOOST_CHECK(TmpL == (MaxL << i));

        shiftArrayRight(TmpArray, MaxArray, 32, i);
        BOOST_CHECK(arith_uint256V(std::vector<unsigned char>(TmpArray,TmpArray+32)) == (MaxL >> i));
        TmpL = MaxL; TmpL >>= i;
        BOOST_CHECK(TmpL == (MaxL >> i));
    }
    arith_uint256 c1L = arith_uint256(0x0123456789abcdefULL);
    arith_uint256 c2L = c1L << 128;
    for (unsigned int i = 0; i < 128; ++i) {
        BOOST_CHECK((c1L << i) == (c2L >> (128-i)));
    }
    for (unsigned int i = 128; i < 256; ++i) {
        BOOST_CHECK((c1L << i) == (c2L << (i-128)));
    }
}

BOOST_AUTO_TEST_CASE( unaryOperators ) // !    ~    -
{
    BOOST_CHECK(~ZeroL == MaxL);
