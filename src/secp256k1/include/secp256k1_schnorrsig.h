#ifndef SECP256K1_SCHNORRSIG_H
#define SECP256K1_SCHNORRSIG_H

#include "secp256k1.h"
#include "secp256k1_extrakeys.h"

#ifdef __cplusplus
extern "C" {
#endif

/** This module implements a variant of Schnorr signatures compliant with
 *  Bitcoin Improvement Proposal 340 "Schnorr Signatures for secp256k1"
 *  (https://github.com/bitcoin/bips/blob/master/bip-0340.mediawiki).
 */

/** A pointer to a function to deterministically generate a nonce.
 *
 *  Same as secp256k1_nonce function with the exception of accepting an
 *  additional pubkey argument and not requiring an attempt argument. The pubkey
 *  argument can protect signature schemes with key-prefixed challenge hash
 *  inputs against reusing the nonce when signing with the wrong precomputed
 *  pubkey.
 *
 *  Returns: 1 if a nonce was successfully generated. 0 will cause signing to
 *           return an error.
 *  Out:  nonce32: pointer to a 32-byte array to be filled by the function
 *  In:       msg: the message being verified. Is NULL if and only if msglen
 *                 is 0.
 *         msglen: the length of the message
 *          key32: pointer to a 32-byte secret key (will not be NULL)
 *     xonly_pk32: the 32-byte serialized xonly pubkey corresponding to key32
 *                 (will not be NULL)
 *           algo: pointer to an array describing the signature
 *                 algorithm (will not be NULL)
 *        algolen: the length of the algo array
 *           data: arbitrary data pointer that is passed through
 *
 *  Except for test cases, this function should compute some cryptographic hash of
 *  the message, the key, the pubkey, the algorithm description, and data.
 */
typedef int (*secp256k1_nonce_function_hardened)(
    unsigned char *nonce32,
    const unsigned char *msg,
    size_t msglen,
    const unsigned char *key32,
    const unsigned char *xonly_pk32,
    const unsigned char *algo,
    size_t algolen,
    void *data
);

/** An implementation of the nonce generation function as defined in Bitcoin
 *  Improvement Proposal 340 "Schnorr Signatures for secp256k1"
 *  (https://github.com/bitcoin/bips/blob/master/bip-0340.mediawiki).
 *
 *  If a data pointer is passed, it is assumed to be a pointer to 32 bytes of
 *  auxiliary random data as defined in BIP-340. If the data pointer is NULL,
 *  the nonce derivation procedure follows BIP-340 by setting the auxiliary
 *  random data to zero. The algo argument must be non-NULL, otherwise the
 *  function will fail and return 0. The hash will be tagged with algo.
 *  Therefore, to create BIP-340 compliant signatures, algo must be set to
 *  "BIP0340/nonce" and algolen to 13.
 */
SECP256K1_API extern const secp256k1_nonce_function_hardened secp256k1_nonce_function_bip340;

/** Data structure that contains additional arguments for schnorrsig_sign_custom.
 *
 *  A schnorrsig_extraparams structure object can be initialized correctly by
 *  setting it to SECP256K1_SCHNORRSIG_EXTRAPARAMS_INIT.
 *
 *  Members:
 *      magic: set to SECP256K1_SCHNORRSIG_EXTRAPARAMS_MAGIC at initialization
 *             and has no other function than making sure the object is
 *             initialized