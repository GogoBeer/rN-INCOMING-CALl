/**********************************************************************
 * Copyright (c) 2018 Pieter Wuille, Greg Maxwell, Gleb Naumenko      *
 * Distributed under the MIT software license, see the accompanying   *
 * file LICENSE or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/


#include <new>

#define MINISKETCH_BUILD
#ifdef _MINISKETCH_H_
#  error "minisketch.h cannot be included before minisketch.cpp"
#endif
#include "../include/minisketch.h"

#include "false_positives.h"
#include "fielddefines.h"
#include "sketch.h"

#ifdef HAVE_CLMUL
#  ifdef _MSC_VER
#    include <intrin.h>
#  else
#    include <cpuid.h>
#  endif
#endif

Sketch* ConstructGeneric1Byte(int bits, int implementation);
Sketch* ConstructGeneric2Bytes(int bits, int implementation);
Sketch* ConstructGeneric3Bytes(int bits, int implementation);
Sketch* ConstructGeneric4Bytes(int bits, int implementation);
Sketch* ConstructGeneric5Bytes(int bits, int implementation);
Sketch* ConstructGeneric6Bytes(int bits, int implementation);
Sketch* ConstructGeneric7Bytes(int bits, int implementation);
Sketch* ConstructGeneric8Bytes(int bits, int implementation);

#ifdef HAVE_CLMUL
Sketch* ConstructClMul1Byte(int bits, int implementation);
Sketch* ConstructClMul2Bytes(int bits, int implementation);
Sketch* ConstructClMul3Bytes(int bits, int implementation);
Sketch* ConstructClMul4Bytes(int bits, int implementation);
Sketch* ConstructClMul5Bytes(int bits, int implementation);
Sketch* ConstructClMul6Bytes(int bits, int implementation);
Sketch* ConstructClMul7Bytes(int bits, int implementation);
Sketch* ConstructClMul8Bytes(int bits, int implementation);
Sketch* ConstructClMulTri1Byte(int bits, int implementation);
Sketch* ConstructClMulTri2Bytes(int bits, int implementation);
Sketch* ConstructClMulTri3Bytes(int bits, int implementation);
Sketch* ConstructClMulTri4Bytes(int bits, int implementation);
Sketch* ConstructClMulTri5Bytes(int bits, int implementation);
Sketch* ConstructClMulTri6Bytes(int bits, int implementation);
Sketch* ConstructClMulTri7Bytes(int bits, int implementation);
Sketch* ConstructClMulTri8Bytes(int bits, int implementation);
#endif

namespace {

enum class FieldImpl {
    GENERIC = 0,
#ifdef HAVE_CLMUL
    CLMUL,
    CLMUL_TRI,
#endif
};

static inline bool EnableClmul()
{
#ifdef HAVE_CLMUL
#ifdef _MSC_VER
    int regs[4];
    __cpuid(regs, 1);
    return (regs[2] & 0x2);
#else
    uint32_t eax, ebx, ecx, edx;
    return (__get_cpuid(1, &eax, &ebx, &ecx, &edx) && (ecx & 0x2));
#endif
#else
    return false;
#endif
}

Sketch* Construct(int bits, int impl)
{
    switch (FieldImpl(impl)) {
    case FieldImpl::GENERIC:
        switch ((bits + 7) / 8) {
        case 1:
            return ConstructGeneric1Byte(bits, impl);
        case 2:
            return ConstructGeneric2Bytes(bits, impl);
        case 3:
            return ConstructGeneric3Bytes(bits, impl);
        case 4:
            return ConstructGeneric4Bytes(bits, impl);
        case 5:
            return ConstructGeneric5Bytes(bits, impl);
        case 6:
            return ConstructGeneric6Bytes(bits, impl);
        case 7:
            return ConstructGeneric7Bytes(bits, impl);
        case 8:
            return ConstructGeneric8Bytes(bits, impl);
        default:
            return nullptr;
        }
        break;
#ifdef HAVE_CLMUL
    case FieldImpl::CLMUL:
        if (EnableClmul()) {
            switch ((bits + 7) / 8) {
            case 1:
                return ConstructClMul1Byte(bits, impl);
            case 2:
                return ConstructClMul2Bytes(bits, impl);
            case 3:
                return ConstructClMul3Bytes(bits, impl);
            case 4:
                return ConstructClMul4Bytes(bits, impl);
            case 5:
                return ConstructClMul5Bytes(bits, impl);
            case 6:
                return ConstructClMul6Bytes(bits, impl);
            case 7:
                return ConstructClMul7Bytes(bits, impl);
            case 8:
                return ConstructClMul8Bytes(bits, impl);
            default:
                return nullptr;
            }
        }
        break;
    case FieldImpl::CLMUL_TRI:
        if (EnableClmul()) {
            switch ((bits + 7) / 8) {
            case 1:
                return ConstructClMulTri1Byte(bits, impl);
            case 2:
                return ConstructClMulTri2Bytes(bits, impl);
            case 3:
                return ConstructClMulTri3Bytes(bits, impl);
            case 4:
                return ConstructClMulTri4Bytes(bits, impl);
            case 5:
                return ConstructClMulTri5Bytes(bits, impl);
            case 6:
                return ConstructClMulTri6Bytes(bits, impl);
            case 7:
                return ConstructClMulTri7Bytes(bits, impl);
            case 8:
                return ConstructClMulTri8Bytes(bits, impl);
            default:
                return nullptr;
            }
        }
        break;
#endif
    }
    return nullptr;
}

}

extern "C" {

int minisketch_bits_supported(uint32_t bits) {
#ifdef ENABLE_FIELD_INT_2
    if (bits == 2) return true;
#endif
#ifdef ENABLE_FIELD_INT_3
    if (bits == 3) return true;
#endif
#ifdef ENABLE_FIELD_INT_4
    if (bits == 4) return true;
#endif
#ifdef ENABLE_FIELD_INT_5
    if (bits == 5) return true;
#endif
#ifdef ENABLE_FIELD_INT_6
    if (bits == 6) return true;
#endif
#ifdef ENABLE_FIELD_INT_7
    if (bits == 7) return true;
#endif
#ifdef ENABLE_FIELD_INT_8
    if (bits == 8) return true;
#endif
#ifdef ENABLE_FIELD_INT_9
    if (bits == 9) return true;
#endif
#ifdef ENABLE_FIELD_INT_10
    if (bits == 10) return true;
#endif
#ifdef ENABLE_FIELD_INT_11
    if (bits == 11) return true;
#endif
#ifdef ENABLE_FIELD_INT_12
    if (bits == 12) return true;
#e