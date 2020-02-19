/**********************************************************************
 * Copyright (c) 2018 Pieter Wuille, Greg Maxwell, Gleb Naumenko      *
 * Distributed under the MIT software license, see the accompanying   *
 * file LICENSE or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

/* This file was substantially auto-generated by doc/gen_params.sage. */
#include "../fielddefines.h"

#if defined(ENABLE_FIELD_BYTES_INT_8)

#include "generic_common_impl.h"

#include "../lintrans.h"
#include "../sketch_impl.h"

#endif

#include "../sketch.h"

namespace {
#ifdef ENABLE_FIELD_INT_57
// 57 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5> StatTable57;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3> DynTable57;
constexpr StatTable57 SQR_TABLE_57({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0x100000000000, 0x400000000000, 0x1000000000000, 0x4000000000000, 0x10000000000000, 0x40000000000000, 0x100000000000000, 0x22, 0x88, 0x220, 0x880, 0x2200, 0x8800, 0x22000, 0x88000, 0x220000, 0x880000, 0x2200000, 0x8800000, 0x22000000, 0x88000000, 0x220000000, 0x880000000, 0x2200000000, 0x8800000000, 0x22000000000, 0x88000000000, 0x220000000000, 0x880000000000, 0x2200000000000, 0x8800000000000, 0x22000000000000, 0x88000000000000, 0x20000000000011, 0x80000000000044});
constexpr StatTable57 QRT_TABLE_57({0xd0c3a82c902426, 0x232aa54103915e, 0x232aa54103915c, 0x1763e291e61699c, 0x232aa541039158, 0x1f424d678bb15e, 0x1763e291e616994, 0x26fd8122f10d36, 0x232aa541039148, 0x1e0a0206002000, 0x1f424d678bb17e, 0x5d72563f39d7e, 0x1763e291e6169d4, 0x1519beb9d597df4, 0x26fd8122f10db6, 0x150c3a87c90e4aa, 0x232aa541039048, 0x15514891f6179d4, 0x1e0a0206002200, 0x14ec9ba7a94c6aa, 0x1f424d678bb57e, 0x1e0f4286382420, 0x5d72563f3957e, 0x4000080000, 0x1763e291e6179d4, 0x1ac0e804882000, 0x1519beb9d595df4, 0x1f430d6793b57e, 0x26fd8122f14db6, 0x3c68e806882000, 0x150c3a87c9064aa, 0x1484fe18b915e, 0x232aa541029048, 0x14f91eb9b595df4, 0x15514891f6379d4, 0x48f6a82380420, 0x1e0a0206042200, 0x14b1beb99595df4, 0x14ec9ba7a9cc6aa, 0x4cf2a82b00420, 0x1f424d679bb57e, 0x26aa0002000000, 0x1e0f4286182420, 0x173f1039dd17df4, 0x5d72563b3957e, 0x4aa0002000000, 0x4000880000, 0x16d31eb9b595df4, 0x1763e291f6179d4, 0x20000000000000, 0x1ac0e806882000, 0x2caa0002000000, 0x1519beb99595df4, 0, 0x1f430d6f93b57e, 0x73e90d6d93b57e, 0x26fd8132f14db6});
typedef Field<uint64_t, 57, 17, StatTable57, DynTable57, &SQR_TABLE_57, &QRT_TABLE_57> Field57;
#endif

#ifdef ENABLE_FIELD_INT_58
// 58 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5> StatTable58;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3> DynTable58;
constexpr StatTable58 SQR_TABLE_58({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0x100000000000, 0x400000000000, 0x1000000000000, 0x4000000000000, 0x10000000000000, 0x40000000000000, 0x100000000000000, 0x80001, 0x200004, 0x800010, 0x2000040, 0x8000100, 0x20000400, 0x80001000, 0x200004000, 0x800010000, 0x2000040000, 0x8000100000, 0x20000400000, 0x80001000000, 0x200004000000, 0x800010000000, 0x2000040000000, 0x8000100000000, 0x20000400000000, 0x80001000000000, 0x200004000000000, 0x10000100002, 0x40000400008, 0x100001000020, 0x400004000080, 0x1000010000200, 0x4000040000800, 0x10000100002000, 0x40000400008000, 0x100001000020000});
constexpr StatTab