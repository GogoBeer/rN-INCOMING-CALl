#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''
Test script for symbol-check.py
'''
import os
import subprocess
from typing import List
import unittest

from utils import determine_wellknown_cmd

def call_symbol_check(cc: List[str], source, executable, options):
    # This should behave the same as AC_TRY_LINK, so arrange well-known flags
    # in the same order as autoconf would.
    #
    # See the definitions for ac_link in autoconf's lib/autoconf/c.m4 file for
    # reference.
    env_flags: List[str] = []
    for var in ['CFLAGS', 'CPPFLAGS', 'LDFLAGS']:
        env_flags += filter(None, os.environ.get(var, '').split(' '))

    subprocess.run([*cc,source,'-o',executable] + env_flags + options, check=True)
    p = subprocess.run(['./contrib/devtools/symbol-check.py',executable], stdout=subprocess.PIPE, universal_newlines=True)
    os.remove(source)
    os.remove(executable)
    return (p.returncode, p.stdout.rstrip())

def get_machine(cc: List[str]):
    p = subprocess.run([*cc,'-dumpmachine'], stdout=subprocess.PIPE, universal_newlines=True)
    return p.stdout.rstrip()

class TestSymbolChecks(unittest.TestCase):
    def test_ELF(self):
        source = 'test1.c'
        executable = 'test1'
        cc = determine_wellknown_cmd('CC', 'gcc')

        # there's no way to do this test for RISC-V at the moment; we build for
        # RISC-V in a glibc 2.27 envinonment and we allow all symbols from 2.27.
        if 'riscv' in get_machine(cc):
            self.skipTest("test not available for RISC-V")

        # nextup was introduced in GLIBC 2.24, so is newer than our supported
        # glibc (2.18), and available in our release build environment (2.24).
        with open(source, 'w', encoding="utf8") as f:
            f.write('''
                #define _GNU_SOURCE
                #include <math.h>

                double nextup(double x);

                int main()
                {
                    nextup(3.14);
                    return 0;
                }
        ''')

        self.assertEqual(call_symbol_check(cc, source, executable, ['-lm']),
                (1, executable + ': symbol nextup from unsupported version GLIBC_2.24(3)\n' +
                    executable + ': failed IMPORTED_SYMBOLS'))

        # -lutil is part of the libc6 package so a safe bet that it's installed
        # it's also out of context enough that it's unlikely to ever become a real dependency
        source = 'test2.c'
        executable = 'test2'
        with open(source, 'w', encoding="utf8") as f:
            f.write('''
                #include <utmp.h>

                int main()
                {
                    login(0);
                    return 0;
                }
        ''')

        self.assertEqual(call_symbol_check(cc, source, executable, ['-lutil']),
                (1, executable + ': libutil.so.1 is not in ALLOWED_LIBRARIES!\n' +
                    executable + ': failed LIBRARY_DEPENDENCIES'))

        # finally, check a simple conforming binary
        source = 'test3.c'
        executable = 'test3'
        with open(source, 'w', encoding="utf8") as f:
            f.write('''
                #include <stdio.h>

                int main()
                {
                    printf("42");
                    return 0;
                }
        ''')

        self.assertEqual(call_symbol_check(cc, source, executable, []),
                (0, ''))

    def test_MACHO(sel