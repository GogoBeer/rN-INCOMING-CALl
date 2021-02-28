AC_PREREQ([2.60])
AC_INIT([libsecp256k1],[0.1])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux/m4])
AC_CANONICAL_HOST
AH_TOP([#ifndef LIBSECP256K1_CONFIG_H])
AH_TOP([#define LIBSECP256K1_CONFIG_H])
AH_BOTTOM([#endif /*LIBSECP256K1_CONFIG_H*/])
AM_INIT_AUTOMAKE([foreign subdir-objects])

LT_INIT([win32-dll])

# Make the compilation flags quiet unless V=1 is used.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

PKG_PROG_PKG_CONFIG

AC_PATH_TOOL(AR, ar)
AC_PATH_TOOL(RANLIB, ranlib)
AC_PATH_TOOL(STRIP, strip)

AM_PROG_CC_C_O
AC_PROG_CC_C89
if test x"$ac_cv_prog_cc_c89" = x"no"; then
  AC_MSG_ERROR([c89 compiler support required])
fi
AM_PROG_AS

case $host_os in
  *darwin*)
     if  test x$cross_compiling != xyes; then
       AC_CHECK_PROG([BREW], brew, brew)
       if test x$BREW = xbrew; then
         # These Homebrew packages may be keg-only, meaning that they won't be found
         # in expected paths because they may conflict with system files. Ask
         # Homebrew where each one is located, then adjust paths accordingly.
         if $BREW list --versions valgrind >/dev/null; then
           valgrind_prefix=$($BREW --prefix valgrind 2>/dev/null)
           VALGRIND_CPPFLAGS="-I$valgrind_prefix/include"
         fi
       else
         AC_CHECK_PROG([PORT], port, port)
         # If homebrew isn't installed and macports is, add the macports default paths
         # as a last resort.
         if test x$PORT = xport; then
           CPPFLAGS="$CPPFLAGS -isystem /opt/local/include"
           LDFLAGS="$LDFLAGS -L/opt/local/lib"
         fi
       fi
     fi
   ;;
esac

# Try if some desirable compiler flags are supported and append them to SECP_CFLAGS.
#
# These are our own flags, so we append them to our own SECP_CFLAGS variable (instead of CFLAGS) as
# recommended in the automake manual (Section "Flag Variables Ordering"). CFLAGS belongs to the user
# and we are not supposed to touch it. In the Makefile, we will need to ensure that SECP_CFLAGS
# is prepended to CFLAGS when invoking the compiler so that the user always has the last word (flag).
#
# Another advantage of not touching CFLAGS is that the contents of CFLAGS will be picked up by
# libtool for compiling helper executables. For example, when compiling for Windows, libtool will
# generate entire wrapper executables (instead of simple wrapper scripts as on Unix) to ensure
# proper operation of uninstalled programs linked by libtool against the uninstalled shared library.
# These executables are compiled from C source file for which our flags may not be appropriate,
# e.g., -std=c89 flag has lead to undesirable warnings in the past.
#
# TODO We should analogously not touch CPPFLAGS and LDFLAGS but currently there are no issues.
AC_DEFUN([SECP_TRY_APPEND_DEFAULT_CFLAGS], [
    # Try to append -Werror=unknown-warning-option to CFLAGS temporarily. Otherwise clang will
    # not error out if it gets unknown warning flags and the checks here will always succeed
    # no matter if clang knows the flag or not.
    SECP_TRY_APPEND_DEFAULT_CFLAGS_saved_CFLAGS="$CFLAGS"
    SECP_TRY_APPEND_CFLAGS([-Werror=unknown-warning-option], CFLAGS)

    SECP_TRY_APPEND_CFLAGS([-std=c89 -pedantic -Wno-long-long -Wnested-externs -Wshadow -Wstrict-prototypes -Wundef], $1) # GCC >= 3.0, -Wlong-long is implied by -pedantic.
    SECP_TRY_APPEND_CFLAGS([-Wno-overlength-strings], $1) # GCC >= 4.2, -Woverlength-strings is implied by -pedantic.
    SECP_TRY_APPEND_CFLAGS([-Wall], $1) # GCC >= 2.95 and probably many other compilers
    SECP_TRY_APPEND_CFLAGS([-Wno-unused-function], $1) # GCC >= 3.0, -Wunused-function is implied by -Wall.
    SECP_TRY_APPEND_CFLAGS([-Wextra], $1) # GCC >= 3.4, this is the newer name of -W, which we don't use because older GCCs will warn about unused functions.
    SECP_TRY_APPEND_CFLAGS([-Wcast-align], $1) # GCC >= 2.95
    SECP_TRY_APPEND_CFLAGS([-Wcast-align=strict], $1) # GCC >= 8.0
    SECP_TRY_APPEND_CFLAGS([-Wconditional-uninitialized], $1) # Clang >= 3.0 only
    SECP_TRY_APPEND_CFLAGS([-fvisibility=hidden], $1) # GCC >= 4.0

    CFLAGS="$SECP_TRY_APPEND_DEFAULT_CFLAGS_saved_CFLAGS"
])
SECP_TRY_APPEND_DEFAULT_CFLAGS(SECP_CFLAGS)

###
### Define config arguments
###

AC_ARG_ENABLE(benchmark,
    AS_HELP_STRING([--enable-benchmark],[compile benchmark [default=yes]]),
    [use_benchmark=$enableval],
    [use_benchmark=yes])

AC_ARG_ENABLE(coverage,
    AS_HELP_STRING([--enable-coverage],[enable compiler flags to support kcov coverage analysis [default=no]]),
    [enable_coverage=$enableval],
    [enable_coverage=no])

AC_ARG_ENABLE(tests,
    AS_HELP_STRING([--enable-tests],[compile tests [default=yes]]),
    [use_tests=$enableval],
    [use_tests=yes])

AC_ARG_ENABLE(experimental,
    AS_HELP_STRING([--enable-experimental],[allow experimental configure options [default=no]]),
    [use_experimental=$enableval],
    [use_experimental=no])

AC_ARG_ENABLE(exhaustive_tests,
    AS_HELP_STRING([--enable-exhaustive-tests],[compile exhaustive tests [default=yes]]),
    [use_exhaustive_tests=$enableval],
    [use_exhaustive_tests=yes])

AC_ARG_ENABLE(module_ecdh,
    AS_HELP_STRING([--enable-module-ecdh],[enable ECDH shared secret computation]),
    [enable_module_ecdh=$enableval],
    [enable_module_ecdh=no])

AC_ARG_ENABLE(module_recovery,
    AS_HELP_STRING([--enable-module-recovery],[enable ECDSA pubkey recovery module [default=no]]),
    [enable_module_recovery=$enableval],
    [enable_module_recovery=no])

AC_ARG_ENABLE(module_extrakeys,
    AS_HELP_STRING([--enable-module-extrakeys],[enable extrakeys module (experimental)]),
    [enable_module_extrakeys=$enableval],
    [enable_module_extrakeys=no])

AC_ARG_ENABLE(module_schnorrsig,
    AS_HELP_STRING([--enable-module-schnorrsig],[enable schnorrsig module (experimental)]),
    [enable_module_schnorrsig=$enableval],
    [enable_module_schnorrsig=no])

AC_ARG_ENABLE(external_default_callbacks,
    AS_HELP_STRING([--enable-external-default-callbacks],[enable external default callback functions [default=no]]),
    [use_external_default_callbacks=$enableval],
    [use_external_default_callbacks=no])

# Test-only override of the (autodetected by the C code) "widemul" setting.
# Legal values are int64 (for [u]int64_t), int128 (for [unsigned] __int128), and auto (the default).
AC_ARG_WITH([test-override-wide-multiply], [] ,[set_widemul=$withval], [set_widemul=auto])

AC_ARG_WITH([asm], [AS_HELP_STRING([--with-asm=x86_64|arm|no|auto],
[assembly optimizations to use (experimental: arm) [default=auto]])],[req_asm=$withval], [req_asm=auto])

AC_ARG_WITH([ecmult-window], [AS_HELP_STRING([--with-ecmult-window=SIZE|auto],
[window size for ecmult precomputation for verification, specified as integer in range [2..24].]
[Larger values result in possibly better performance at the cost of an exponentially larger precomputed table.]
[The table will store 2^(SIZE-1) * 64 bytes of data but can be larger in memory due to platform-specific padding and alignment.]
[A window size larger than 15 will require you delete the prebuilt ecmult_static_pre_g.h file so that it can be rebuilt.]
[For very large window sizes, use "make -j 1" to reduce memory use during compilation.]
["auto" is a reasonable setting for desktop machines (currently 15). [default=auto]]
)],
[req_ecmult_window=$withval], [req_ecmult_window=auto])

AC_ARG_WITH([ecmult-gen-precision], [AS_HELP_STRING([--with-ecmult-gen-precision=2|4|8|auto],
[Precision bits to tune the precomputed table size for signing.]
[The size of the table is 32kB for 2 bits, 64kB for 4 bits, 512kB for 8 bits of precision.]
[A larger table size usually results in possible faster signing.]
["auto" is a reasonable setting for desktop machines (currently 4). [default=auto]]
)],
[req_ecmult_gen_precision=$withval], [req_ecmult_gen_precision=auto])

AC_ARG_WITH([valgrind], [AS_HELP_STRING([--with-valgrind=yes|no|auto],
[Build with extra checks for running inside Valgrind [default=auto]]
)],
[req_valgrind=$withval], [req_valgrind=auto])

###
### Handle config options (except for modules)
###

if test x"$req_valgrind" = x"no"; then
  enable_valgrind=no
else
  SECP_VALGRIND_CHECK
  if test x"$has_valgrind" != x"yes"; then
    if test x"$req_valgrind" = x"yes"; then
      AC_MSG_ERROR([Valgrind support explicitly requested but valgrind/memcheck.h header not available])
    fi
    enable_valgrind=no
  else
    enable_valgrind=yes
