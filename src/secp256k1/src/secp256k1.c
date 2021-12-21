/***********************************************************************
 * Copyright (c) 2013-2015 Pieter Wuille                               *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#define SECP256K1_BUILD

#include "../include/secp256k1.h"
#include "../include/secp256k1_preallocated.h"

#include "assumptions.h"
#include "util.h"
#include "field_impl.h"
#include "scalar_impl.h"
#include "group_impl.h"
#include "ecmult_impl.h"
#include "ecmult_const_impl.h"
#include "ecmult_gen_impl.h"
#include "ecdsa_impl.h"
#include "eckey_impl.h"
#include "hash_impl.h"
#include "scratch_impl.h"
#include "selftest.h"

#ifdef SECP256K1_NO_BUILD
# error "secp256k1.h processed without SECP256K1_BUILD defined while building secp256k1.c"
#endif

#if defined(VALGRIND)
# include <valgrind/memcheck.h>
#endif

#define ARG_CHECK(cond) do { \
    if (EXPECT(!(cond), 0)) { \
        secp256k1_callback_call(&ctx->illegal_callback, #cond); \
        return 0; \
    } \
} while(0)

#define ARG_CHECK_NO_RETURN(cond) do { \
    if (EXPECT(!(cond), 0)) { \
        secp256k1_callback_call(&ctx->illegal_callback, #cond); \
    } \
} while(0)

struct secp256k1_context_struct {
    secp256k1_ecmult_gen_context ecmult_gen_ctx;
    secp256k1_callback illegal_callback;
    secp256k1_callback error_callback;
    int declassify;
};

static const secp256k1_context secp256k1_context_no_precomp_ = {
    { 0 },
    { secp256k1_default_illegal_callback_fn, 0 },
    { secp256k1_default_error_callback_fn, 0 },
    0
};
const secp256k1_context *secp256k1_context_no_precomp = &secp256k1_context_no_precomp_;

size_t secp256k1_context_preallocated_size(unsigned int flags) {
    size_t ret = sizeof(secp256k1_context);
    /* A return value of 0 is reserved as an indicator for errors when we call this function internally. */
    VERIFY_CHECK(ret != 0);

    if (EXPECT((flags & SECP256K1_FLAGS_TYPE_MASK) != SECP256K1_FLAGS_TYPE_CONTEXT, 0)) {
            secp256k1_callback_call(&default_illegal_callback,
                                    "Invalid flags");
            return 0;
    }

    return ret;
}

size_t secp256k1_context_preallocated_clone_size(const secp256k1_context* ctx) {
    size_t ret = sizeof(secp256k1_context);
    VERIFY_CHECK(ctx != NULL);
    return ret;
}

secp256k1_context* secp256k1_context_preallocated_create(void* prealloc, unsigned int flags) {
    size_t prealloc_size;
    secp256k1_context* ret;

    if (!secp256k1_selftest()) {
        secp256k1_callback_call(&default_error_callback, "self test failed");
    }

    prealloc_size = secp256k1_context_preallocated_size(flags);
    if (prealloc_size == 0) {
        return NULL;
    }
    VERIFY_CHECK(prealloc != NULL);
    ret = (secp256k1_context*)prealloc;
    ret->illegal_callback = default_illegal_callback;
    ret->error_callback = default_error_callback;

    /* Flags have been checked by secp256k1_context_preallocated_size. */
    VERIFY_CHECK((flags & SECP256K1_FLAGS_TYPE_MASK) == SECP256K1_FLAGS_TYPE_CONTEXT);
    secp256k1_ecmult_gen_context_build(&ret->ecmult_gen_ctx);
    ret->declassify = !!(flags & SECP256K1_FLAGS_BIT_CONTEXT_DECLASSIFY);

    return ret;
}

secp256k1_context* secp256k1_context_create(unsigned int flags) {
    size_t const prealloc_size = secp256k1_context_preallocated_size(flags);
    secp256k1_context* ctx = (secp256k1_context*)checked_malloc(&default_error_callback, prealloc_size);
    if (EXPECT(secp256k1_context_preallocated_create(ctx, flags) == NULL, 0)) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

secp256k1_context* secp256k1_context_preallocated_clone(const secp256k1_context* ctx, void* prealloc) {
    secp256k1_context* ret;
    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(prealloc != NULL);

    ret = (secp256k1_context*)prealloc;
    *ret = *ctx;
    return ret;
}

secp256k1_context* secp256k1_context_clone(const secp256k1_context* ctx) {
    secp256k1_context* ret;
    size_t prealloc_size;

    VERIFY_CHECK(ctx != NULL);
    prealloc_size = secp256k1_context_preallocated_clone_size(ctx);
    ret = (secp256k1_context*)checked_malloc(&ctx->error_callback, prealloc_size);
    ret = secp256k1_context_preallocated_clone(ctx, ret);
    return ret;
}

void secp256k1_context_preallocated_destroy(secp256k1_context* ctx) {
    ARG_CHECK_NO_RETURN(ctx != secp256k1_context_no_precomp);
    if (ctx != NULL) {
        secp256k1_ecmult_gen_context_clear(&ctx->ecmult_gen_ctx);
    }
}

void secp256k1_context_destroy(secp256k1_context* ctx) {
    if (ctx != NULL) {
        secp256k1_context_preallocated_destroy(ctx);
        free(ctx);
    }
}

void secp256k1_context_set_illegal_callback(secp256k1_context* ctx, void (*fun)(const char* message, void* data), const void* data) {
    ARG_CHECK_NO_RETURN(ctx != secp256k1_context_no_precomp);
    if (fun == NULL) {
        fun = secp256k1_default_illegal_callback_fn;
    }
    ctx->illegal_callback.fn = fun;
    ctx->illegal_callback.data = data;
}

void secp256k1_context_set_error_callback(secp256k1_context* ctx, void (*fun)(const char* message, void* data), const void* data) {
    ARG_CHECK_NO_RETURN(ctx != secp256k1_context_no_precomp);
    if (fun == NULL) {
        fun = secp256k1_default_error_callback_fn;
    }
    ctx->error_callback.fn = fun;
    ctx->error_callback.data = data;
}

secp256k1_scratch_space* secp256k1_scratch_space_create(const secp256k1_context* ctx, size_t max_size) {
    VERIFY_CHECK(ctx != NULL);
    return secp256k1_scratch_create(&ctx->error_callback, max_size);
}

void secp256k1_scratch_space_destroy(const secp256k1_context *ctx, secp256k1_scratch_space* scratch) {
    VERIFY_CHECK(ctx != NULL);
    secp256k1_scratch_destroy(&ctx->error_callback, scratch);
}

/* Mark memory as no-longer-secret for the purpose of analysing constant-time behaviour
 *  of the software. This is setup for use with valgrind but could be substituted with
 *  the appropriate instrumentation for other analysis tools.
 */
static SECP256K1_INLINE void secp256k1_declassify(const secp256k1_context* ctx, const void *p, size_t len) {
#if defined(VALGRIND)
    if (EXPECT(ctx->declassify,0)) VALGRIND_MAKE_MEM_DEFINED(p, len);
#else
    (void)ctx;
    (void)p;
    (void)len;
#endif
}

static int secp256k1_pubkey_load(const secp256k1_context* ctx, secp256k1_ge* ge, const secp256k1_pubkey* pubkey) {
    if (sizeof(secp256k1_ge_storage) == 64) {
        /* When the secp256k1_ge_storage type is exactly 64 byte, use its
         * representation inside secp256k1_pubkey, as conversion is very fast.
         * Note that secp256k1_pubkey_save must use the same representation. */
        secp256k1_ge_storage s;
        memcpy(&s, &pubkey->data[0], sizeof(s));
        secp256k1_ge_from_storage(ge, &s);
    } else {
        /* Otherwise, fall back to 32-byte big endian for X and Y. */
        secp256k1_fe x, y;
        secp256k1_fe_set_b32(&x, pubkey->data);
        secp256k1_fe_set_b32(&y, pubkey->data + 32);
        secp256k1_ge_set_xy(ge, &x, &y);
    }
    ARG_CHECK(!secp256k1_fe_is_zero(&ge->x));
    return 1;
}

static void secp256k1_pubkey_save(secp256k1_pubkey* pubkey, secp256k1_ge* ge) {
    if (sizeof(secp256k1_ge_storage) == 64) {
        secp256k1_ge_storage s;
        secp256k1_ge_to_storage(&s, ge);
        memcpy(&pubkey->data[0], &s, sizeof(s));
    } else {
        VERIFY_CHECK(!secp256k1_ge_is_infinity(ge));
        secp256k1_fe_normalize_var(&ge->x);
        secp256k1_fe_normalize