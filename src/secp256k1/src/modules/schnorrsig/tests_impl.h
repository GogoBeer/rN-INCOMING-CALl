
/***********************************************************************
 * Copyright (c) 2018-2020 Andrew Poelstra, Jonas Nick                 *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#ifndef SECP256K1_MODULE_SCHNORRSIG_TESTS_H
#define SECP256K1_MODULE_SCHNORRSIG_TESTS_H

#include "../../../include/secp256k1_schnorrsig.h"

/* Checks that a bit flip in the n_flip-th argument (that has n_bytes many
 * bytes) changes the hash function
 */
void nonce_function_bip340_bitflip(unsigned char **args, size_t n_flip, size_t n_bytes, size_t msglen, size_t algolen) {
    unsigned char nonces[2][32];
    CHECK(nonce_function_bip340(nonces[0], args[0], msglen, args[1], args[2], args[3], algolen, args[4]) == 1);
    secp256k1_testrand_flip(args[n_flip], n_bytes);
    CHECK(nonce_function_bip340(nonces[1], args[0], msglen, args[1], args[2], args[3], algolen, args[4]) == 1);