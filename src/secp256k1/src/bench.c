/***********************************************************************
 * Copyright (c) 2014 Pieter Wuille                                    *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#include <stdio.h>
#include <string.h>

#include "../include/secp256k1.h"
#include "util.h"
#include "bench.h"

void help(int default_iters) {
    printf("Benchmarks the following algorithms:\n");
    printf("    - ECDSA signing/verification\n");

#ifdef ENABLE_MODULE_ECDH
    printf("    - ECDH key exchange (optional module)\n");
#endif

#ifdef ENABLE_MODULE_RECOVERY
    printf("    - Public key recovery (optional module)\n");
#endif

#ifdef ENABLE_MODULE_SCHNORRSIG
    printf("    - Schnorr signatures (optional module)\n");
#endif

    printf("\n");
    printf("The default number of iterations for each benchmark is %d. This can be\n", default_iters);
    printf("customized using the SECP256K1_BENCH_ITERS environment variable.\n");
    printf("\n");
    printf("Usage: ./bench [args]\n");
    printf("By default, all benchmarks will be run.\n");
    printf("args:\n");
    printf("    help              : display this help and exit\n");
    printf("    ecdsa             : all ECDSA algorithms--sign, verify, recovery (if enabled)\n");
    printf("    ecdsa_sign        : ECDSA siging algorithm\n");
    printf("    ecdsa_verify      : ECDSA verification algorithm\n");

#ifdef ENABLE_MODULE_RECOVERY
    printf("    ecdsa_recover     : ECDSA public key recovery algorithm\n");
#endif

#ifdef ENABLE_MODULE_ECDH
    printf("    ecdh              : ECDH key exchange algorithm\n");
#endif

#ifdef ENABLE_MODULE_SCHNORRSIG
    printf("    schnorrsig        : all Schnorr signature algorithms (sign, verify)\n");
    printf("    schnorrsig_sign   : Schnorr sigining algorithm\n");
    printf("    schnorrsig_verify : Schnorr verification algorithm\n");
#endif

    printf("\n");
}

typedef struct {
    secp256k1_context *ctx;
    unsigned char msg[32];
    unsigned char key[32];
    unsigned char sig[72];
    size_t siglen;
    unsigned char pubkey[33];
    size_t pubkeylen;
} bench_verify_data;

static void bench_verify(void* arg, int iters) {
    int i;
    bench_verify_data* data = (bench_verify_data*)arg;

    for (i = 0; i < iters; i++) {
        secp256k1_pubkey pubkey;
        secp256k1_ecdsa_signature sig;
        data->sig[data->siglen - 1] ^= (i & 0xFF);
        data->sig[data->siglen - 2] ^= ((i >> 8) & 0xFF);
        data->sig[data->siglen - 3] ^= ((i >> 16) & 0xFF);
        CHECK(secp256k1_ec_pubkey_parse(data->ctx, &pubkey, data->pubkey, data->pubkeylen) == 1);
        CHECK(secp256k1_ecdsa_signature_parse_der(data->ctx, &sig, data->sig, data->siglen) == 1);
        CHECK(secp256k1_ecdsa_verify(data->ctx, &sig, data->msg, &pubkey) == (i == 0));
        data->sig[data->siglen - 1] ^= (i & 0xFF);
        data->sig[data->siglen - 2] ^= ((i >> 8) & 0xFF);
        data->sig[data->siglen - 3] ^= ((i >> 16) & 0xFF);
    }
}

typedef struct {
    secp256k1_context* ctx;
    unsigned char msg[32];
    unsigned char key[32];
} bench_sign_d