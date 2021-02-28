#ifndef SECP256K1_PREALLOCATED_H
#define SECP256K1_PREALLOCATED_H

#include "secp256k1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The module provided by this header file is intended for settings in which it
 * is not possible or desirable to rely on dynamic memory allocation. It provides
 * functions for creating, cloning, and destroying secp256k1 context objects in a
 * contiguous fixed-size block of memory provided by the caller.
 *
 * Context objects created by functions in this module can be used like contexts
 * objects created by functions in secp256k1.h, i.e., they can be passed to any
 * API function that expects a context object (see secp256k1.h for details). The
 * only exception is that context objects created by functions in this module
 * must be destroyed using secp256k1_context_preallocated_destroy (in this
 * module) instead of secp256k1_context_destroy (in secp256k1.h).
 *
 * It is guaranteed that functions in this module will not call malloc or its
 * friends realloc, calloc, and free.
 */

/** Determine 