#!/usr/bin/env python3
# Copyright (c) 2014 Wladimir J. van der Laan
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''
A script to check that release executables only contain certain symbols
and are only linked against allowed libraries.

Example usage:

    find ../path/to/binaries -type f -executable | xargs python3 contrib/devtools/symbol-check.py
'''
import sys
from typing import List, Dict

import lief #type:ignore

# temporary constant, to be replaced with lief.ELF.ARCH.RISCV
# https://github.com/lief-project/LIEF/pull/562
LIEF_ELF_ARCH_RISCV = lief.ELF.ARCH(243)

# Debian 9 (Stretch) EOL: 2022. https://wiki.debian.org/DebianReleases#Production_Releases
#
# - g++ version 6.3.0 (https://packages.debian.org/search?suite=stretch&arch=any&searchon=names&keywords=g%2B%2B)
# - libc version 2.24 (https://packages.debian.org/search?suite=stretch&arch=any&searchon=names&keywords=libc6)
#
# Ubuntu 16.04 (Xenial) EOL: 2026. https://wiki.ubuntu.com/Releases
#
# - g++ version 5.3.1
# - libc version 2.23
#
# CentOS Stream 8 EOL: 2024. https://wiki.centos.org/About/Product
#
# - g++ version 8.5.0 (http://mirror.centos.org/centos/8-stream/AppStream/x86_64/os/Packages/)
# - libc version 2.28 (http://mirror.centos.org/centos/8-stream/AppStream/x86_64/os/Packages/)
#
# See https://gcc.gnu.org/onlinedocs/libstdc++/manual/abi.html for more info.

MAX_VERSIONS = {
'GCC':       (4,8,0),
'GLIBC': {
    lief.ELF.ARCH.i386:   (2,18),
    lief.ELF.ARCH.x86_64: (2,18),
    lief.ELF.ARCH.ARM:    (2,18),
    lief.ELF.ARCH.AARCH64:(2,18),
    lief.ELF.ARCH.PPC64:  (2,18),
    LIEF_ELF_ARCH_RISCV:  (2,27),
},
'LIBATOMIC': (1,0),
'V':         (0,5,0),  # xkb (bitcoin-qt only)
}
# See here for a description of _IO_stdin_used:
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=634261#109

# Ignore symbols that are exported as part of every executable
IGNORE_EXPORTS = {
'_edata', '_end', '__end__', '_init', '__bss_start', '__bss_start__', '_bss_end__',
'__bss_end__', '_fini', '_IO_stdin_used', 'stdin', 'stdout', 'stderr',
'environ', '_environ', '__environ',
}

# Expected linker-loader names can be found here:
# https://sourceware.org/glibc/wiki/ABIList?action=recall&rev=16
ELF_INTERPRETER_NAMES: Dict[lief.ELF.ARCH, Dict[lief.ENDIANNESS, str]] = {
    lief.ELF.ARCH.i386:    {
        lief.ENDIANNESS.LITTLE: "/lib/ld-linux.so.2",
    },
    lief.ELF.ARCH.x86_64:  {
        lief.ENDIANNESS.LITTLE: "/lib64/ld-linux-x86-64.so.2",
    },
    lief.ELF.ARCH.ARM:     {
        lief.ENDIANNESS.LITTLE: "/lib/ld-linux-armhf.so.3",
    },
    lief.ELF.ARCH.AARCH64: {
        lief.ENDIANNESS.LITTLE: "/lib/ld-linux-aarch64.so.1",
    },
    lief.ELF.ARCH.PPC64:   {
        lief.ENDIANNESS.BIG: "/lib64/ld64.so.1",
        lief.ENDIANNESS.LITTLE: "/lib64/ld64.so.2",
    },
    LIEF_ELF_ARCH_RISCV:    {
        lief.ENDIANNESS.LITTLE: "/lib/ld-linux-riscv64-lp64d.so.1",
    },
}

# Allowed NEEDED libraries
ELF_ALLOWED_LIBRARIES = {
# bitcoind and bitcoin-qt
'libgcc_s.so.1', # GCC base support
'libc.so.6', # C library
'libpthread.so.0', # threading
'libm.so.6', # math library
'librt.so.1', # real-time (clock)
'libatomic.so.1',
'ld-linux-x86-64.so.2', # 64-bit dynamic linker
'ld-linux.so.2', # 32-bit dynamic linker
'ld-linux-aarch64.so.1', # 64-bit ARM dynamic linker
'ld-linux-armhf.so.3', # 32-bit ARM dynamic linker
'ld64.so.1', # POWER64 ABIv1 dynamic linker
'ld64.so.2', # POWER64 ABIv2 dynamic linker
'ld-linux-riscv64-lp64d.so.1', # 64-bit RISC-V dynamic linker
# bitcoin-qt only
'libxcb.so.1', # part of X11
'libxkbcommon.so.0', # keyboard keymapping
'libxkbcommon-x11.so.0', # keyboard keymapping
'libfontconfig.so.1', # font support
'libfreetype.so.6', # font parsing
'libdl.so.2', # programming interface to dynamic linker
'libxcb-icccm.so.4',
'libxcb-image.so.0',
'libxcb-shm.so.0',
'libxcb-keysyms.so.1',
'libxcb-randr.so.0',
'libxcb-render-util.so.0',
'libxcb-render.so.0',
'libxcb-shape.so.0',
'libxcb-sync.so.1',
'libxcb-xfixes.so.0',
'libxcb-xinerama.so.0',
'libxcb-xkb.so.1',
}

MACHO_ALLOWED_LIBRARIES = {
# bitcoind and bitcoin-qt
'libc++.1.dylib', # C++ Standard Library
'libSystem.B.dylib', # libc, libm, libpthread, libinfo
# bitcoin-qt only
'AppKit', # user interface
'ApplicationServices', # common application tasks.
'Carbon', # deprecated c back-compat API
'ColorSync',
'CoreFoundation', # low level func, data types
'CoreGraphics', # 2D rendering
'CoreServices', # operating system services
'CoreText', # interface for laying out text and handling fonts.
'CoreVideo', # video processing
'Foundation', # base layer functionality for a