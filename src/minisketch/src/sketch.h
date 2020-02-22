/**********************************************************************
 * Copyright (c) 2018 Pieter Wuille, Greg Maxwell, Gleb Naumenko      *
 * Distributed under the MIT software license, see the accompanying   *
 * file LICENSE or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#ifndef _MINISKETCH_STATE_H_
#define _MINISKETCH_STATE_H_

#include <stdint.h>
#include <stdlib.h>

/** Abstract class for internal representation of a minisketch object. */
class Sketch
{
    uint64_t m_canary;
    const int m_implementation;
    const int m_bits;

public:
    Sketch(int implementation, int bits) : m_implementation(implementation), m_bits(bits) {}

    void Ready() { m_canary = 0x6d496e536b65LU; }
    void Check() const { if (m_canary != 0x