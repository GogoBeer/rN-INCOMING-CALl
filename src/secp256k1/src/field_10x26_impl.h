/***********************************************************************
 * Copyright (c) 2013, 2014 Pieter Wuille                              *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#ifndef SECP256K1_FIELD_REPR_IMPL_H
#define SECP256K1_FIELD_REPR_IMPL_H

#include "util.h"
#include "field.h"
#include "modinv32_impl.h"

#ifdef VERIFY
static void secp256k1_fe_verify(const secp256k1_fe *a) {
    const uint32_t *d = a->n;
    int m = a->normalized ? 1 : 2 * a->magnitude, r = 1;
    r &= (d[0] <= 0x3FFFFFFUL * m);
    r &= (d[1] <= 0x3FFFFFFUL * m);
    r &= (d[2] <= 0x3FFFFFFUL * m);
    r &= (d[3] <= 0x3FFFFFFUL * m);
    r &= (d[4] <= 0x3FFFFFFUL * m);
    r &= (d[5] <= 0x3FFFFFFUL * m);
    r &= (d[6] <= 0x3FFFFFFUL * m);
    r &= (d[7] <= 0x3FFFFFFUL * m);
    r &= (d[8] <= 0x3FFFFFFUL * m);
    r &= (d[9] <= 0x03FFFFFUL * m);
    r &= (a->magnitude >= 0);
    r &= (a->magnitude <= 32);
    if (a->normalized) {
        r &= (a->magnitude <= 1);
        if (r && (d[9] == 0x03FFFFFUL)) {
            uint32_t mid = d[8] & d[7] & d[6] & d[5] & d[4] & d[3] & d[2];
            if (mid == 0x3FFFFFFUL) {
                r &= ((d[1] + 0x40UL + ((d[0] + 0x3D1UL) >> 26)) <= 0x3FFFFFFUL);
            }
        }
    }
    VERIFY_CHECK(r == 1);
}
#endif

static void secp256k1_fe_normalize(secp256k1_fe *r) {
    uint32_t t0 = r->n[0], t1 = r->n[1], t2 = r->n[2], t3 = r->n[3], t4 = r->n[4],
             t5 = r->n[5], t6 = r->n[6], t7 = r->n[7], t8 = r->n[8], t9 = r->n[9];

    /* Reduce t9 at the start so there will be at most a single carry from the first pass */
    uint32_t m;
    uint32_t x = t9 >> 22; t9 &= 0x03FFFFFUL;

    /* The first pass ensures the magnitude is 1, ... */
    t0 += x * 0x3D1UL; t1 += (x << 6);
    t1 += (t0 >> 26); t0 &= 0x3FFFFFFUL;
    t2 += (t1 >> 26); t1 &= 0x3FFFFFFUL;
    t3 += (t2 >> 26); t2 &= 0x3FFFFFFUL; m = t2;
    t4 += (t3 >> 26); t3 &= 0x3FFFFFFUL; m &= t3;
    t5 += (t4 >> 26); t4 &= 0x3FFFFFFUL; m &= t4;
    t6 += (t5 >> 26); t5 &= 0x3FFFFFFUL; m &= t5;
    t7 += (t6 >> 26); t6 &= 0x3FFFFFFUL; m &= t6;
    t8 += (t7 >> 26); t7 &= 0x3FFFFFFUL; m &= t7;
    t9 += (t8 >> 26); t8 &= 0x3FFFFFFUL; m &= t8;

    /* ... except for a possible carry at bit 22 of t9 (i.e. bit 256 of the field element) */
    VERIFY_CHECK(t9 >> 23 == 0);

    /* At most a single final reduction is needed; check if the value is >= the field characteristic */
    x = (t9 >> 22) | ((t9 == 0x03FFFFFUL) & (m == 0x3FFFFFFUL)
        & ((t1 + 0x40UL + ((t0 + 0x3D1UL) >> 26)) > 0x3FFFFFFUL));

    /* Apply the final reduction (for constant-time behaviour, we do it always) */
    t0 += x * 0x3D1UL; t1 += (x << 6);
    t1 += (t0 >> 26); t0 &= 0x3FFFFFFUL;
    t2 += (t1 >> 26); t1 &= 0x3FFFFFFUL;
    t3 += (t2 >> 26); t2 &= 0x3FFFFFFUL;
    t4 += (t3 >> 26); t3 &= 0x3FFFFFFUL;
    t5 += (t4 >> 26); t4 &= 0x3FFFFFFUL;
    t6 += (t5 >> 26); t5 &= 0x3FFFFFFUL;
    t7 += (t6 >> 26); t6 &= 0x3FFFFFFUL;
    t8 += (t7 >> 26); t7 &= 0x3FFFFFFUL;
    t9 += (t8 >> 26); t8 &= 0x3FFFFFFUL;

    /* If t9 didn't carry to bit 22 already, then it should have after any final reduction */
    VERIFY_CHECK(t9 >> 22 == x);

    /* Mask off the possible multiple of 2^256 from the final reduction */
    t9 &= 0x03FFFFF