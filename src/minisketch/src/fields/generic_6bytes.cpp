
/**********************************************************************
 * Copyright (c) 2018 Pieter Wuille, Greg Maxwell, Gleb Naumenko      *
 * Distributed under the MIT software license, see the accompanying   *
 * file LICENSE or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

/* This file was substantially auto-generated by doc/gen_params.sage. */
#include "../fielddefines.h"

#if defined(ENABLE_FIELD_BYTES_INT_6)

#include "generic_common_impl.h"

#include "../lintrans.h"
#include "../sketch_impl.h"

#endif

#include "../sketch.h"

namespace {
#ifdef ENABLE_FIELD_INT_41
// 41 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 6, 5> StatTable41;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3> DynTable41;
constexpr StatTable41 SQR_TABLE_41({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x12, 0x48, 0x120, 0x480, 0x1200, 0x4800, 0x12000, 0x48000, 0x120000, 0x480000, 0x1200000, 0x4800000, 0x12000000, 0x48000000, 0x120000000, 0x480000000, 0x1200000000, 0x4800000000, 0x12000000000, 0x8000000012});
constexpr StatTable41 QRT_TABLE_41({0, 0x1599a5e0b0, 0x1599a5e0b2, 0x105c119e0, 0x1599a5e0b6, 0x1a2030452a6, 0x105c119e8, 0x1a307c55b2e, 0x1599a5e0a6, 0x1ee3f47bc8e, 0x1a203045286, 0x400808, 0x105c119a8, 0x1a3038573a6, 0x1a307c55bae, 0x4d2882a520, 0x1599a5e1a6, 0x1ffbaa0b720, 0x1ee3f47be8e, 0x4d68c22528, 0x1a203045686, 0x200006, 0x400008, 0x1b79a21b200, 0x105c109a8, 0x1ef3886a526, 0x1a3038553a6, 0x1b692209200, 0x1a307c51bae, 0x5d99a4e1a6, 0x4d28822520, 0x185e109ae, 0x1599a4e1a6, 0x4e3f43be88, 0x1ffbaa2b720, 0x4000000000, 0x1ee3f43be8e, 0x18000000006, 0x4d68ca2528, 0xa203145680, 0x1a203145686});
typedef Field<uint64_t, 41, 9, StatTable41, DynTable41, &SQR_TABLE_41, &QRT_TABLE_41> Field41;
#endif

#ifdef ENABLE_FIELD_INT_42
// 42 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 6, 6> StatTable42;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3> DynTable42;
constexpr StatTable42 SQR_TABLE_42({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x81, 0x204, 0x810, 0x2040, 0x8100, 0x20400, 0x81000, 0x204000, 0x810000, 0x2040000, 0x8100000, 0x20400000, 0x81000000, 0x204000000, 0x810000000, 0x2040000000, 0x8100000000, 0x20400000000, 0x1000000102, 0x4000000408, 0x10000001020});
constexpr StatTable42 QRT_TABLE_42({0x810200080, 0x120810806, 0x120810804, 0x1068c1a1000, 0x120810800, 0x34005023008, 0x1068c1a1008, 0x800004080, 0x120810810, 0x162818a10, 0x34005023028, 0x42408a14, 0x1068c1a1048, 0x1001040, 0x800004000, 0xb120808906, 0x120810910, 0x34000020068, 0x162818810, 0x68c021400, 0x34005023428, 0x10004000, 0x42408214, 0x162418214, 0x1068c1a0048, 0xb002018116, 0x1003040, 0x10008180448, 0x800000000, 0x62c08b04, 0xb120800906, 0x2408d1a3060, 0x120800910, 0x34401003028, 0x34000000068, 0, 0x162858810, 0xa042058116, 0x68c0a1400, 0x8162858806, 0x34005123428, 0x3068c0a1468});
typedef Field<uint64_t, 42, 129, StatTable42, DynTable42, &SQR_TABLE_42, &QRT_TABLE_42> Field42;
#endif

#ifdef ENABLE_FIELD_INT_43
// 43 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 5, 5, 5, 5, 5> StatTable43;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3> DynTable43;
constexpr StatTable43 SQR_TABLE_43({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0xb2, 0x2c8, 0xb20, 0x2c80, 0xb200, 0x2c800, 0xb2000, 0x2c8000, 0xb20000, 0x2c80000, 0xb200000, 0x2c800000, 0xb2000000, 0x2c8000000, 0xb20000000, 0x2c80000000, 0xb200000000, 0x2c800000000, 0x32000000059, 0x4800000013d, 0x20000000446});
constexpr StatTable43 QRT_TABLE_43({0x2bccc2d6f6c, 0x4bccc2d6f54, 0x4bccc2d6f56, 0x7cc7bc61df0, 0x4bccc2d6f52, 0x7d13b404b10, 0x7cc7bc61df8, 0x37456e9ac5a, 0x4bccc2d6f42, 0x4e042c6a6, 0x7d13b404b30, 0x4a56de9ef4c, 0x7cc7bc61db8, 0x14bc18d8e, 0x37456e9acda, 0x7c89f84fb1e, 0x4bccc2d6e42, 0x7ffae40d210, 0x4e042c4a6, 0x366f45dd06, 0x7d13b404f30, 0x496fcaf8cca, 0x4a56de9e74c, 0x370b62b6af4, 0x7cc7bc60db8, 0x1498185a8, 0x14bc1ad8e, 0x7e602c46a98, 0x37456e9ecda, 0x36ccc2c6e74, 0x7c89f847b1e, 0x7e27d06d516, 0x4bccc2c6e42, 0x7f93302c396, 0x7ffae42d210, 0x3dd3440706, 0x4e046c4a6, 0x78bbc09da36, 0x366f4ddd06, 0, 0x7d13b504f30, 0x8bbc09da00, 0x496fc8f8cca});
typedef Field<uint64_t, 43, 89, StatTable43, DynTable43, &SQR_TABLE_43, &QRT_TABLE_43> Field43;
#endif

#ifdef ENABLE_FIELD_INT_44
// 44 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 5, 5, 5, 5> StatTable44;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4> DynTable44;
constexpr StatTable44 SQR_TABLE_44({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0x21, 0x84, 0x210, 0x840, 0x2100, 0x8400, 0x21000, 0x84000, 0x210000, 0x840000, 0x2100000, 0x8400000, 0x21000000, 0x84000000, 0x210000000, 0x840000000, 0x2100000000, 0x8400000000, 0x21000000000, 0x84000000000, 0x10000000042, 0x40000000108});
constexpr StatTable44 QRT_TABLE_44({0xf05334f4f6e, 0x4002016, 0x4002014, 0xf04350e6246, 0x4002010, 0x4935b379a26, 0xf04350e624e, 0xf84250c228e, 0x4002000, 0xf04300e521e, 0x4935b379a06, 0xb966838dd48, 0xf04350e620e, 0xf7b8b80feda, 0xf84250c220e, 0xf972e097d5e, 0x4002100, 0x8000020000, 0xf04300e501e, 0x430025000, 0x4935b379e06, 0xf976a09dc5e, 0xb966838d548, 0xf84218c029a, 0xf04350e720e, 0x4925f36bf06, 0xf7b8b80deda, 0xb047d3ee758, 0xf84250c620e, 0xf80350e720e, 0xf972e09fd5e, 0x8091825284, 0x4012100, 0x9015063210, 0x8000000000, 0xff31a028c5e, 0xf04300a501e, 0x44340b7100, 0x4300a5000, 0, 0x4935b279e06, 0xa976b2dce18, 0xf976a29dc5e, 0x8935b279e18});
typedef Field<uint64_t, 44, 33, StatTable44, DynTable44, &SQR_TABLE_44, &QRT_TABLE_44> Field44;
#endif

#ifdef ENABLE_FIELD_INT_45
// 45 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 5, 5, 5> StatTable45;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3> DynTable45;
constexpr StatTable45 SQR_TABLE_45({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0x100000000000, 0x36, 0xd8, 0x360, 0xd80, 0x3600, 0xd800, 0x36000, 0xd8000, 0x360000, 0xd80000, 0x3600000, 0xd800000, 0x36000000, 0xd8000000, 0x360000000, 0xd80000000, 0x3600000000, 0xd800000000, 0x36000000000, 0xd8000000000, 0x16000000001b, 0x18000000005a});
constexpr StatTable45 QRT_TABLE_45({0xede34e3e0fc, 0x1554148191aa, 0x1554148191a8, 0x1767be1dc4a6, 0x1554148191ac, 0x26bd4931492, 0x1767be1dc4ae, 0x233ab9c454a, 0x1554148191bc, 0x16939e8bb3dc, 0x26bd49314b2, 0x3c6ca8bac52, 0x1767be1dc4ee, 0x16caa5054c16, 0x233ab9c45ca, 0x14a1649628bc, 0x1554148190bc, 0x3c382881252, 0x16939e8bb1dc, 0x3c7ca0aa160, 0x26bd49310b2, 0x27f40158000, 0x3c6ca8ba452, 0x173fc092853c, 0x1767be1dd4ee, 0x16cbe284f25c, 0x16caa5056c16, 0x155559002f96, 0x233ab9c05ca, 0x26eb8908b32, 0x14a16496a8bc, 0x15440885333c, 0x1554148090bc, 0x17d60702e0, 0x3c3828a1252, 0x54548d10b2, 0x16939e8fb1dc, 0x3ac1e81b1d2, 0x3c7ca02a160, 0x166bd48310bc, 0x26bd48310b2, 0, 0x27f40358000, 0x10000000000e, 0x3c6cacba452});
typedef Field<uint64_t, 45, 27, StatTable45, DynTable45, &SQR_TABLE_45, &QRT_TABLE_45> Field45;
#endif

#ifdef ENABLE_FIELD_INT_46
// 46 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 6, 5, 5> StatTable46;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3> DynTable46;
constexpr StatTable46 SQR_TABLE_46({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0x100000000000, 0x3, 0xc, 0x30, 0xc0, 0x300, 0xc00, 0x3000, 0xc000, 0x30000, 0xc0000, 0x300000, 0xc00000, 0x3000000, 0xc000000, 0x30000000, 0xc0000000, 0x300000000, 0xc00000000, 0x3000000000, 0xc000000000, 0x30000000000, 0xc0000000000, 0x300000000000});
constexpr StatTable46 QRT_TABLE_46({0x211c4fd486ba, 0x100104a, 0x1001048, 0x104d0492d4, 0x100104c, 0x20005040c820, 0x104d0492dc, 0x40008080, 0x100105c, 0x24835068ce00, 0x20005040c800, 0x200000400800, 0x104d04929c, 0x100904325c, 0x40008000, 0x25da9e77daf0, 0x100115c, 0x1184e1696f0, 0x24835068cc00, 0x24825169dd5c, 0x20005040cc00, 0x3ea3241c60c0, 0x200000400000, 0x211c4e5496f0, 0x104d04829c, 0x20005340d86c, 0x100904125c, 0x24835968de5c, 0x4000c000, 0x6400a0c0, 0x25da9e775af0, 0x118cf1687ac, 0x101115c, 0x1ea1745cacc0, 0x1184e1496f0, 0x20181e445af0, 0x2483506ccc00, 0x20240060c0, 0x24825161dd5c, 0x1e21755dbd9c, 0x20005050cc00, 0x26a3746cacc0, 0x3ea3243c60c0, 0xea3243c60c0, 0x200000000000, 0});
typedef Field<uint64_t, 46, 3, StatTable46, DynTable46, &SQR_TABLE_46, &QRT_TABLE_46> Field46;
#endif

#ifdef ENABLE_FIELD_INT_47
// 47 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 6, 6, 5> StatTable47;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3> DynTable47;
constexpr StatTable47 SQR_TABLE_47({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0x100000000000, 0x400000000000, 0x42, 0x108, 0x420, 0x1080, 0x4200, 0x10800, 0x42000, 0x108000, 0x420000, 0x1080000, 0x4200000, 0x10800000, 0x42000000, 0x108000000, 0x420000000, 0x1080000000, 0x4200000000, 0x10800000000, 0x42000000000, 0x108000000000, 0x420000000000, 0x80000000042, 0x200000000108});
constexpr StatTable47 QRT_TABLE_47({0, 0x1001040, 0x1001042, 0x1047043076, 0x1001046, 0x112471c241e, 0x104704307e, 0x4304e052168, 0x1001056, 0x10004000, 0x112471c243e, 0x172a09c949d6, 0x104704303e, 0x4002020, 0x4304e0521e8, 0x5400e220, 0x1001156, 0x172b08c85080, 0x10004200, 0x41200b0800, 0x112471c203e, 0x172f0cca50a0, 0x172a09c941d6, 0x7eb88a11c1d6, 0x104704203e, 0x1044042020, 0x4000020, 0x42001011156, 0x4304e0561e8, 0x172a28c95880, 0x54006220, 0x112931cc21e, 0x1011156, 0x53670f283e, 0x172b08ca5080, 0x7a80c414a03e, 0x10044200, 0x40000000000, 0x4120030800, 0x1928318801e, 0x112470c203e, 0x799283188000, 0x172f0cea50a0, 0x1eb88a91c1c8, 0x172a098941d6, 0x3ea8cc95e1f6, 0x7eb88a91c1d6});
typedef Field<uint64_t, 47, 33, StatTable47, DynTable47, &SQR_TABLE_47, &QRT_TABLE_47> Field47;
#endif

#ifdef ENABLE_FIELD_INT_48
// 48 bit field
typedef RecLinTrans<uint64_t, 6, 6, 6, 6, 6, 6, 6, 6> StatTable48;
typedef RecLinTrans<uint64_t, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4> DynTable48;
constexpr StatTable48 SQR_TABLE_48({0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000, 0x4000000, 0x10000000, 0x40000000, 0x100000000, 0x400000000, 0x1000000000, 0x4000000000, 0x10000000000, 0x40000000000, 0x100000000000, 0x400000000000, 0x2d, 0xb4, 0x2d0, 0xb40, 0x2d00, 0xb400, 0x2d000, 0xb4000, 0x2d0000, 0xb40000, 0x2d00000, 0xb400000, 0x2d000000, 0xb4000000, 0x2d0000000, 0xb40000000, 0x2d00000000, 0xb400000000, 0x2d000000000, 0xb4000000000, 0x2d0000000000, 0xb40000000000, 0xd0000000005a, 0x40000000011f});
constexpr StatTable48 QRT_TABLE_48({0xc00442c284f0, 0xc16b7fda410a, 0xc16b7fda4108, 0xada3b5c79fbe, 0xc16b7fda410c, 0x16f3c18d5b0, 0xada3b5c79fb6, 0x7090a381f64, 0xc16b7fda411c, 0xcafc15d179f8, 0x16f3c18d590, 0x6630880e534e, 0xada3b5c79ff6, 0xa13dd1f49826, 0x7090a381fe4, 0xb87560f6a74, 0xc16b7fda401c, 0xaaaaffff0012, 0xcafc15d17bf8, 0xaafd15f07bf6, 0x16f3c18d190, 0x60000020000e, 0x6630880e5b4e, 0xcb977fcb401c, 0xada3b5c78ff6, 0x6663420cad0, 0xa13dd1f4b826, 0xc0045fc2f41c, 0x7090a385fe4, 0x6762e24b834, 0xb87560fea74, 0xc6351fed241c, 0xc16b7fdb401c, 0x60065622ea7a, 0xaaaafffd0012, 0xdf9562bea74, 0xcafc15d57bf8, 0x6657ea057bea, 0xaafd15f87bf6, 0xa79329ddaa66, 0x16f3c08d190, 0xa39229f0aa66, 0x60000000000e, 0x175fb4468ad0, 0x6630884e5b4e, 0, 0xcb977f4b401c, 0x2630884e5b40});
typedef Field<uint64_t, 48, 45, StatTable48, DynTable48, &SQR_TABLE_48, &QRT_TABLE_48> Field48;
#endif
}

Sketch* ConstructGeneric6Bytes(int bits, int implementation)
{
    switch (bits) {
#ifdef ENABLE_FIELD_INT_41
    case 41: return new SketchImpl<Field41>(implementation, 41);
#endif
#ifdef ENABLE_FIELD_INT_42
    case 42: return new SketchImpl<Field42>(implementation, 42);
#endif
#ifdef ENABLE_FIELD_INT_43
    case 43: return new SketchImpl<Field43>(implementation, 43);
#endif
#ifdef ENABLE_FIELD_INT_44
    case 44: return new SketchImpl<Field44>(implementation, 44);
#endif
#ifdef ENABLE_FIELD_INT_45
    case 45: return new SketchImpl<Field45>(implementation, 45);
#endif
#ifdef ENABLE_FIELD_INT_46
    case 46: return new SketchImpl<Field46>(implementation, 46);
#endif
#ifdef ENABLE_FIELD_INT_47
    case 47: return new SketchImpl<Field47>(implementation, 47);
#endif
#ifdef ENABLE_FIELD_INT_48
    case 48: return new SketchImpl<Field48>(implementation, 48);
#endif
    default: return nullptr;
    }
}