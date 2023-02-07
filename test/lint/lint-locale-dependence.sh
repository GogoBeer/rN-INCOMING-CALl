#!/usr/bin/env bash
# Copyright (c) 2018-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C

# Be aware that bitcoind and bitcoin-qt differ in terms of localization: Qt
# opts in to POSIX localization by running setlocale(LC_ALL, "") on startup,
# whereas no such call is made in bitcoind.
#
# Qt runs setlocale(LC_ALL, "") on initialization. This installs the locale
# specified by the user's LC_ALL (or LC_*) environment variable as the new
# C locale.
#
# In contrast, bitcoind does not opt in to localization -- no call to
# setlocale(LC_ALL, "") is made and the environment variables LC_* are
# thus ignored.
#
# This results in situations where bitcoind is guaranteed to be running
# with the classic locale ("C") whereas the locale of bitcoin-qt will vary
# depending on the user's environment variables.
#
# An example: Assuming the environment variable LC_ALL=de_DE then the
# call std::to_string(1.23) will return "1.230000" in bitcoind but
# "1,230000" in bitcoin-qt.
#
# From the Qt documentation:
# "On Unix/Linux Qt is configured to use the system locale settings by default.
#  This can cause a conflict when using POSIX functions, for instance, when
#  converting between data types such as floats and strings, since the notation
#  may differ between locales. To get around this problem, call the POSIX function
#  setlocale(LC_NUMERIC,"C") right after initializing QApplication, QGuiApplication
#  or QCoreApplication to reset the locale that is used for number formatting to
#  "C"-locale."
#
# See https://doc.qt.io/qt-5/qcoreapplication.html#locale-settings and
# https://stackoverflow.com/a/34878283 for more details.

# TODO: Reduce KNOWN_VIOLATIONS by replacing uses of locale dependent snprintf with strprintf.
KNOWN_VIOLATIONS=(
    "src/dbwrapper.cpp:.*vsnprintf"
    "src/test/dbwrapper_tests.cpp:.*snprintf"
    "src/test/fuzz/locale.cpp"
    "src/test/fuzz/string.cpp"
)

REGEXP_IGNORE_EXTERNAL_DEPENDENCIES="^src/(crypto/ctaes/|leveldb/|secp256k1/|minisketch/|tinyformat.h|univalue/)"

LOCALE_DEPENDENT_FUNCTIONS=(
    alphasort    # LC_COLLATE (via strcoll)
    asctime      # LC_TIME (directly)
    asprintf     # (via vasprintf)
    atof         # LC_NUMERIC (via strtod)
    atoi         # LC_NUMERIC (via strtol)
    atol         # LC_NUMERIC (via strtol)
    atoll        # (via strtoll)
    atoq
    btowc        # LC_CTYPE (directly)
    ctime        # (via asctime or localtime)
    dprintf      # (via vdprintf)
    fgetwc
    fgetws
    fold_case    # boost::locale::fold_case
    fprintf      # (via vfprintf)
    fputwc
    fputws
    fscanf       # (via __vfscanf)
    fwprintf     # (via __vfwprintf)
    getdate      # via __getdate_r => isspace // __localtime_r
    getwc
    getwchar
    is_digit     # boost::algorithm::is_digit
    is_space     # boost::algorithm::is