/***********************************************************************
 * Copyright (c) 2015 Pieter Wuille, Andrew Poelstra                   *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#ifndef SECP256K1_ECMULT_CONST_IMPL_H
#define SECP256K1_ECMULT_CONST_IMPL_H

#include "scalar.h"
#include "group.h"
#include "ecmult_const.h"
#include "ecmult_impl.h"

/* This is like `ECMULT_TABLE_GET_GE` but is constant time */
#define ECMULT_CONST_TABLE_GET_GE(r,pre,n,w) do { \
    int m = 0; \
    /* Extract the sign-bit for a constant time absolute-value. */ \
    int mask = (n) >> (sizeof(n) * CHAR_BIT - 1); \
    int abs_n = ((n) + mask) ^ mask; \
    int idx_n = abs_n >> 1; \
    secp256k1_fe neg_y; \
    VERIFY_CHECK(((n) & 1) == 1); \
    VERIFY_CHECK((n) >= -((1 << ((w)-1)) - 1)); \
    VERIFY_CHECK((n) <=  ((1 << ((w)-1)) - 1)); \
    VERIFY_SETUP(secp256k1_fe_clear(&(r)->x)); \
    VERIFY_SETUP(secp256k1_fe_clear(&(r)->y)); \
    /* Unconditionally set r->x = (pre)[m].x. r->y = (pre)[m].y. because it's either the correct one \
     * or will get replaced in the later iterations, this is needed to make sure `r` is initialized. */ \
    (r)->x = (pre)[m].x; \
    (r)->y = (pre)[m].y; \
    for (m = 1; m < ECMULT_TABLE_SIZE(w); m++) { \
        /* This loop is used to avoid secret data in array indices. See
         * the comment in ecmult_gen_impl.h for rationale. */ \
        secp256k1_fe_cmov(&(r)->x, &(pre)[m].x, m == idx_n); \
        secp256k1_fe_cmov(&(r)->y, &(pre)[m].y, m == idx_n); \
    } \
    (r)->infinity = 0; \
    secp256k1_fe_negate(&neg_y, &(r)->y, 1); \
    secp256k1_fe_cmov(&(r)->y, &neg_y, (n) != abs_n); \
} while(0)


/** Convert a number to WNAF notation.
 *  The number becomes represented by sum(2^{wi} * wnaf[i], i=0..WNAF_SIZE(w)+1) - return_val.
 *  It has the following guarantees:
 *  - each wnaf[i] an odd integer between -(1 << w) and (1 << w)
 *  - each wnaf[i] is nonzero
 *  - the number of words set is always WNAF_SIZE(w) + 1
 *
 *  Adapted from `The Width-w NAF Method Provides Small Memory and Fast Elliptic Scalar
 *  Multiplications Secure against Side Channel Attacks`, Okeya and Tagaki. M. Joye (Ed.)
 *  CT-RSA 2003, LNCS 2612, pp. 328-443, 2003. Springer-Verlag Berlin Heidelberg 2003
 *
 *  Numbers reference steps of `Algorithm SPA-resistant Width-w NAF with Odd Scalar` on pp. 335
 */
static int secp256k1_wnaf_const(int *wnaf, const secp256k1_scalar *scalar, int w, int size) {
    int global_sign;
    int skew = 0;
    int word = 0;

    /* 1 2 3 */
    int u_last;
    int u;

    int flip;
    int bit;
    secp256k1_scalar s;
    int not_neg_one;

    VERIFY_CHECK(w > 0);
    VERIFY_CHECK(size > 0);

    /* Note that we cannot handle even numbers by negating them to be odd, as is
     * done in other implementations, since if our scalars were specified to have
     * width < 256 for performance reasons, their negations would have width 256
     * and we'd lose any performance benefit. Instead, we use a technique from
     * Section 4.2 of the Okeya/Tagaki paper, which is to add either 1 (for even)
     * or 2 (for odd) to the number we are encoding, returning a skew value indicating
     * this, and having the caller compensate after doing the multiplication.
     *
     * In fact, we _do_ want to negate numbers to minimize their bit-lengths (and in
     * particular, to ensure that the outputs from the endomorphism-split fit into
     * 128 bits). If we negate, the parity of our number flips, inverting which of
     * {1, 2} we want to add to the scalar when ensuring that it's odd. Further
     * complicating things, -1 interacts badly with `secp256k1_scalar_cadd_bit` and
     * we need to special-case it in this logic. */
    flip = secp256k1_scalar_is_high(scalar);
    /* We add 1 to even numbers, 2 to odd ones, noting that negation flips parity */
    bit = flip ^ !secp256k1_scalar_is_even(scalar);
    /* We check for negative one, since adding 2 to it will cause an overflow */
    secp256k1_scalar_negate(&s, scalar);
    not_neg_one = !secp256k1_scalar_is_one(&s);
    s = *scalar;
    secp256k1_scalar_cadd_bit(&s, bit, not_neg_one);
    /* If we had negative one, flip == 1, s.d[0] == 0, bit == 1, so caller expects
     * that we added two to it and flipped it. In fact for -1 these operations are
     * identical. We only flipped, but since skewing is required (in the sense that
     * the skew must be 1 or 2, never zero) and flipping is not, we need to change
     * our flags to claim that we only skewed. */
    global_sign = secp256k1_scalar_cond_negate(&s, flip);
    global_sign *= not_neg_one * 2 - 1;
    skew = 1 << bit;

    /* 4 */
    u_last = secp256k1_scalar_shr_int(&s, w);
    do {
        int even;

        /* 4.1 4.4 */
        u = secp256k1_scalar_shr_int(&s, w);
        /* 4.2 */
        even = ((u & 1) == 0);
        /* In contrast to the original algorithm, u_last is always > 0 and
         * therefore we do not need to check its sign. In particular, it's easy
         * to see that u_last is never < 0 because u is never < 0. Moreover,
         * u_last is never = 0 because u is never even after a loop
         * iteration. The same holds analogously for the initial value of
         * u_last (in the first loop iteration). */
        VERIFY_CHECK(u_last > 0);
        VERIFY_CHECK((u_last & 1) == 1);
        u += even;
        u_last -= even * (1 << w);

        /* 4.3, adapted for global sign change */
        