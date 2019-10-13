//  __   _ _______ __   _  _____  ______  _______ __   _ _______ _     _
//  | \  | |_____| | \  | |     | |_____] |______ | \  | |       |_____|
//  |  \_| |     | |  \_| |_____| |_____] |______ |  \_| |_____  |     |
//
// Microbenchmark framework for C++11/14/17/20
// https://github.com/martinus/nanobench
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019-2021 Martin Ankerl <martin.ankerl@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef ANKERL_NANOBENCH_H_INCLUDED
#define ANKERL_NANOBENCH_H_INCLUDED

// see https://semver.org/
#define ANKERL_NANOBENCH_VERSION_MAJOR 4 // incompatible API changes
#define ANKERL_NANOBENCH_VERSION_MINOR 3 // backwards-compatible changes
#define ANKERL_NANOBENCH_VERSION_PATCH 6 // backwards-compatible bug fixes

///////////////////////////////////////////////////////////////////////////////////////////////////
// public facing api - as minimal as possible
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>  // high_resolution_clock
#include <cstring> // memcpy
#include <iosfwd>  // for std::ostream* custom output target in Config
#include <string>  // all names
#include <vector>  // holds all results

#define ANKERL_NANOBENCH(x) ANKERL_NANOBENCH_PRIVATE_##x()

#define ANKERL_NANOBENCH_PRIVATE_CXX() __cplusplus
#define ANKERL_NANOBENCH_PRIVATE_CXX98() 199711L
#define ANKERL_NANOBENCH_PRIVATE_CXX11() 201103L
#define ANKERL_NANOBENCH_PRIVATE_CXX14() 201402L
#define ANKERL_NANOBENCH_PRIVATE_CXX17() 201703L

#if ANKERL_NANOBENCH(CXX) >= ANKERL_NANOBENCH(CXX17)
#    define ANKERL_NANOBENCH_PRIVATE_NODISCARD() [[nodiscard]]
#else
#    define ANKERL_NANOBENCH_PRIVATE_NODISCARD()
#endif

#if defined(__clang__)
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_PUSH() \
        _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wpadded\"")
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_POP() _Pragma("clang diagnostic pop")
#else
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_PUSH()
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_PADDED_POP()
#endif

#if defined(__GNUC__)
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_PUSH() _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Weffc++\"")
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_POP() _Pragma("GCC diagnostic pop")
#else
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_PUSH()
#    define ANKERL_NANOBENCH_PRIVATE_IGNORE_EFFCPP_POP()
#endif

#if defined(ANKERL_NANOBENCH_LOG_ENABLED)
#    include <iostream>
#    define ANKERL_NANOBENCH_LOG(x)                                                 \
        do {                                                                        \
            std::cout << __FUNCTION__ << "@" << __LINE__ << ": " << x << std::endl; \
        } while (0)
#else
#    define ANKERL_NANOBENCH_LOG(x) \
        do {                        \
        } while (0)
#endif

#define ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS() 0
#if defined(__linux__) && !defined(ANKERL_NANOBENCH_DISABLE_PERF_COUNTERS)
#    include <linux/version.h>
#    if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
// PERF_COUNT_HW_REF_CPU_CYCLES only available since kernel 3.3
// PERF_FLAG_FD_CLOEXEC since kernel 3.14
#        undef ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS
#        define ANKERL_NANOBENCH_PRIVATE_PERF_COUNTERS() 1
#    endif
#endif

#if defined(__clang__)
#    define ANKERL_NANOBENCH_NO_SANITIZE(...) __attribute__((no_sanitize(__VA_ARGS__)))
#else
#    define ANKERL_NANOBENCH_NO_SANITIZE(...)
#endif

#if defined(_MSC_VER)
#    define ANKERL_NANOBENCH_PRIVATE_NOINLINE() __declspec(noinline)
#else
#    define ANKERL_NANOBENCH_PRIVATE_NOINLINE() __attribute__((noinline))
#endif

// workaround missing "is_trivially_copyable" in g++ < 5.0
// See https://stackoverflow.com/a/31798726/48181
#if defined(__GNUC__) && __GNUC__ < 5
#    define ANKERL_NANOBENCH_IS_TRIVIALLY_COPYABLE(...) __has_trivial_copy(__VA_ARGS__)
#else
#    define ANKERL_NANOBENCH_IS_TRIVIALLY_COPYABLE(...) std::is_trivially_copyable<__VA_ARGS__>::value
#endif

// declarations ///////////////////////////////////////////////////////////////////////////////////

namespace ankerl {
namespace nanobench {

using Clock = std::conditional<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock,
                               std::chrono::steady_clock>::type;
class Bench;
struct Config;
class Result;
class Rng;
class BigO;

/**
 * @brief Renders output from a mustache-like template and benchmark results.
 *
 * The templating facility here is heavily inspired by [mustache - logic-less templates](https://mustache.github.io/).
 * It adds a few more features that are necessary to get all of the captured data out of nanobench. Please read the
 * excellent [mustache manual](https://mustache.github.io/mustache.5.html) to see what this is all about.
 *
 * nanobench output has two nested layers, *result* and *measurement*.  Here is a hierarchy of the allowed tags:
 *
 * * `{{#result}}` Marks the begin of the result layer. Whatever comes after this will be instantiated as often as
 *   a benchmark result is available. Within it, you can use these tags:
 *
 *    * `{{title}}` See Bench::title().
 *
 *    * `{{name}}` Benchmark name, usually directly provided with Bench::run(), but can also be set with Bench::name().
 *
 *    * `{{unit}}` Unit, e.g. `byte`. Defaults to `op`, see Bench::title().
 *
 *    * `{{batch}}` Batch size, see Bench::batch().
 *
 *    * `{{complexityN}}` Value used for asymptotic complexity calculation. See Bench::complexityN().
 *
 *    * `{{epochs}}` Number of epochs, see Bench::epochs().
 *
 *    * `{{clockResolution}}` Accuracy of the clock, i.e. what's the smallest time possible to measure with the clock.
 *      For modern systems, this can be around 20 ns. This value is automatically determined by nanobench at the first
 *      benchmark that is run, and used as a static variable throughout the application's runtime.
 *
 *    * `{{clockResolutionMultiple}}` Configuration multiplier for `clockResolution`. See Bench::clockResolutionMultiple().
 *      This is the target runtime for each measurement (epoch). That means the more accurate your clock is, the faster
 *      will be the benchmark. Basing the measurement's runtime on the clock resolution is the main reason why nanobench is so fast.
 *
 *    * `{{maxEpochTime}}` Configuration for a maximum time each measurement (epoch) is allowed to take. Note that at least
 *      a single iteration will be performed, even when that takes longer than maxEpochTime. See Bench::maxEpochTime().
 *
 *    * `{{minEpochTime}}` Minimum epoch time, usually not set. See Bench::minEpochTime().
 *
 *    * `{{minEpochIterations}}` See Bench::minEpochIterations().
 *
 *    * `{{epochIterations}}` See Bench::epochIterations().
 *
 *    * `{{warmup}}` Number of iterations used before measuring starts. See Bench::warmup().
 *
 *    * `{{relative}}` True or false, depending on 