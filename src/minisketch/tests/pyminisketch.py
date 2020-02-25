#!/usr/bin/env python3
# Copyright (c) 2020 Pieter Wuille
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

"""Native Python (slow) reimplementation of libminisketch' algorithms."""

import random
import unittest

# Irreducible polynomials over GF(2) to use (represented as integers).
#
# Most fields can be defined by multiple such polynomials. Minisketch uses the one with the minimal
# number of nonzero coefficients, and tie-breaking by picking the lexicographically first among
# those.
#
# All polynomials for degrees 2 through 64 (inclusive) are given.
GF2_MODULI = [
    None, None,
    2**2 + 2**1 + 1,
    2**3 + 2**1 + 1,
    2**4 + 2**1 + 1,
    2**5 + 2**2 + 1,
    2**6 + 2**1 + 1,
    2**7 + 2**1 + 1,
    2**8 + 2**4 + 2**3 + 2**1 + 1,
    2**9 + 2**1 + 1,
    2**10 + 2**3 + 1,
    2**11 + 2**2 + 1,
    2**12 + 2**3 + 1,
    2**13 + 2**4 + 2**3 + 2**1 + 1,
    2**14 + 2**5 + 1,
    2**15 + 2**1 + 1,
    2**16 + 2**5 + 2**3 + 2**1 + 1,
    2**17 + 2**3 + 1,
    2**18 + 2**3 + 1,
    2**19 + 2**5 + 2**2 + 2**1 + 1,
    2**20 + 2**3 + 1,
    2**21 + 2**2 + 1,
    2**22 + 2**1 + 1,
    2**23 + 2**5 + 1,
    2**24 + 2**4 + 2**3 + 2**1 + 1,
    2**25 + 2**3 + 1,
    2**26 + 2**4 + 2**3 + 2**1 + 1,
    2**27 + 2**5 + 2**2 + 2**1 + 1,
    2**28 + 2**1 + 1,
    2**29 + 2**2 + 1,
    2**30 + 2**1 + 1,
    2**31 + 2**3 + 1,
    2**32 + 2**7 + 2**3 + 2**2 + 1,
    2**33 + 2**10 + 1,
    2**34 + 2**7 + 1,
    2**35 + 2**2 + 1,
    2**36 + 2**9 + 1,
    2**37 + 2**6 + 2**4 + 2**1 + 1,
    2**38 + 2**6 + 2**5 + 2**1 + 1,
    2**39 + 2**4 + 1,
    2**40 + 2**5 + 2**4 + 2**3 + 1,
    2**41 + 2**3 + 1,
    2**42 + 2**7 + 1,
    2**43 + 2**6 + 2**4 + 2**3 + 1,
    2**44 + 2**5 + 1,
    2**45 + 2**4 + 2**3 + 2**1 + 1,
    2**46 + 2**1 + 1,
    2**47 + 2**5 + 1,
    2**48 + 2**5 + 2**3 + 2**2 + 1,
    2**49 + 2**9 + 1,
    2**50 + 2**4 + 2**3 + 2**2 + 1,
    2**51 + 2**6 + 2**3 + 2**1 + 1,
    2**52 + 2**3 + 1,
    2**53 + 2**6 + 2**2 + 2**1 + 1,
    2**54 + 2**9 + 1,
    2**55 + 2**7 + 1,
    2**56 + 2**7 + 2**4 + 2**2 + 1,
    2**57 + 2**4 + 1,
    2**58 + 2**19 + 1,
    2**59 + 2**7 + 2**4 + 2**2 + 1,
    2**60 + 2**1 + 1,
    2**61 + 2**5 + 2**2 + 2**1 + 1,
    2**62 + 2**29 + 1,
    2**63 + 2**1 + 1,
    2**64 + 2**4 + 2**3 + 2**1 + 1
]

class GF2Ops:
    """Class to perform GF(2^field_size) operations on elements represented as integers.

    Given that elements are represented as integers, addition is simply xor, and not
    exposed here.
    """

    def __init__(self, field_size):
        """Construct a GF2Ops object for the specified field size."""
        self.field_size = field_size
        self._modulus = GF2_MODULI[field_size]
        assert self._modulus is not None

    def mul2(self, x):
        """Multiply x by 2 in GF(2^field_size)."""
        x <<= 1
        if x >> self.field_size:
            x ^= self._modulus
        return x

    def mul(self, x, y):
        """Multiply x by y in GF(2^field_size)."""
        ret = 0
        while y:
            if y & 1:
                ret ^= x
            y >>= 1
            x = self.mul2(x)
        return ret

    def sqr(self, x):
        """Square x in GF(2^field_size)."""
        return self.mul(x, x)

    def inv(self, x):
        """Compute the inverse of x in GF(2^field_size)."""
        assert x != 0
        # Use the extended polynomial Euclidean GCD algorithm on (modulus, x), over GF(2).
        # See https://en.wikipedia.org/wiki/Polynomial_greatest_common_divisor.
        t1, t2 = 0, 1
        r1, r2 = self._modulus, x
        r1l, r2l = self.field_size + 1, r2.bit_length()
        while r2:
            q = r1l - r2l
            r1 ^= r2 << q
            t1 ^= t2 << q
            r1l = r1.bit_length()
            if r1 < r2:
                t1, t2 = t2, t1
                r1, r2 = r2, r1
                r1l, r2l = r2l, r1l
        assert r1 == 1
        return t1

class TestGF2Ops(unittest.TestCase):
    """Test class for basic arithmetic properties of GF2Ops."""

    def field_size_test(self, field_size):
        """Test operations for given field_size."""

        gf = GF2Ops(field_size)
        for i in range(100):
            x = random.randrange(1 << field_size)
            y = random.randrange(1 << field_size)
            x2 = gf.mul2(x)
            xy = gf.mul(x, y)
            self.assertEqual(x2, gf.mul(x, 2)) # mul2(x) == x*2
            self.assertEqual(x2, gf.mul(2, x)) # mul2(x) == 2*x
            self.assertEqual(xy == 0, x == 0 or y == 0)
            self.assertEqual(xy == x, y == 1 or x == 0)
            self.assertEqual(xy == y, x == 1 or y == 0)
            self.assertEqual(xy, gf.mul(y, x)) # x*y == y*x
            if i < 10:
                xp = x
                for _ in range(field_size):
                    xp = gf.sqr(xp)
                self.assertEqual(xp, x) # x^(2^field_size) == x
            if y != 0:
                yi = gf.inv(y)
                self.assertEqual(y == yi, y == 1) # y==1/x iff y==1
                self.assertEqual(gf.mul(y, yi), 1) # y*(1/y) == 1
                yii = gf.inv(yi)
                self.assertEqual(y, yii) # 1/(1/y) == y
                if x != 0:
                    xi = gf.inv(x)
                    xyi = gf.inv(xy)
                    self.assertEqual(xyi, gf.mul(xi, yi)) # (1/x)*(1/y) == 1/(x*y)

    def test(self):
        """Run tests."""
        for field_size in range(2, 65):
            self.field_size_test(field_size)

# The operations below operate on polynomials over GF(2^field_size), represented as lists of
# integers:
#
#   [a, b, c, ...] = a + b*x + c