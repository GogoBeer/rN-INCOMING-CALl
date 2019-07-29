Developer Notes
===============

<!-- markdown-toc start -->
**Table of Contents**

- [Developer Notes](#developer-notes)
    - [Coding Style (General)](#coding-style-general)
    - [Coding Style (C++)](#coding-style-c)
    - [Coding Style (Python)](#coding-style-python)
    - [Coding Style (Doxygen-compatible comments)](#coding-style-doxygen-compatible-comments)
      - [Generating Documentation](#generating-documentation)
    - [Development tips and tricks](#development-tips-and-tricks)
        - [Compiling for debugging](#compiling-for-debugging)
        - [Show sources in debugging](#show-sources-in-debugging)
        - [Compiling for gprof profiling](#compiling-for-gprof-profiling)
        - [`debug.log`](#debuglog)
        - [Signet, testnet, and regtest modes](#signet-testnet-and-regtest-modes)
        - [DEBUG_LOCKORDER](#debug_lockorder)
        - [Valgrind suppressions file](#valgrind-suppressions-file)
        - [Compiling for test coverage](#compiling-for-test-coverage)
        - [Performance profiling with perf](#performance-profiling-with-perf)
        - [Sanitizers](#sanitizers)
    - [Locking/mutex usage notes](#lockingmutex-usage-notes)
    - [Threads](#threads)
    - [Ignoring IDE/editor files](#ignoring-ideeditor-files)
- [Development guidelines](#development-guidelines)
    - [General Bitcoin Core](#general-bitcoin-core)
    - [Wallet](#wallet)
    - [General C++](#general-c)
    - [C++ data structures](#c-data-structures)
    - [Strings and formatting](#strings-and-formatting)
    - [Shadowing](#shadowing)
    - [Threads and synchronization](#threads-and-synchronization)
    - [Scripts](#scripts)
        - [Shebang](#shebang)
    - [Source code organization](#source-code-organization)
    - [GUI](#gui)
    - [Subtrees](#subtrees)
    - [Upgrading LevelDB](#upgrading-leveldb)
      - [File Descriptor Counts](#file-descriptor-counts)
      - [Consensus Compatibility](#consensus-compatibility)
    - [Scripted diffs](#scripted-diffs)
        - [Suggestions and examples](#suggestions-and-examples)
    - [Release notes](#release-notes)
    - [RPC interface guidelines](#rpc-interface-guidelines)
    - [Internal interface guidelines](#internal-interface-guidelines)

<!-- markdown-toc end -->

Coding Style (General)
----------------------

Various coding styles have been used during the history of the codebase,
and the result is not very consistent. However, we're now trying to converge to
a single style, which is specified below. When writing patches, favor the new
style over attempting to mimic the surrounding style, except for move-only
commits.

Do not submit patches solely to modify the style of existing code.

Coding Style (C++)
------------------

- **Indentation and whitespace rules** as specified in
[src/.clang-format](/src/.clang-format). You can use the provided
[clang-format-diff script](/contrib/devtools/README.md#clang-format-diffpy)
tool to clean up patches automatically before submission.
  - Braces on new lines for classes, functions, methods.
  - Braces on the same line for everything else.
  - 4 space indentation (no tabs) for every block except namespaces.
  - No indentation for `public`/`protected`/`private` or for `namespace`.
  - No extra spaces inside parenthesis; don't do `( this )`.
  - No space after function names; one space after `if`, `for` and `while`.
  - If an `if` only has a single-statement `then`-clause, it can appear
    on the same line as the `if`, without braces. In every other case,
    braces are required, and the `then` and `else` clauses must appear
    correctly indented on a new line.
  - There's no hard limit on line width, but prefer to keep lines to <100
    characters if doing so does not decrease readability. Break up long
    function declarations over multiple lines using the Clang Format
    [AlignAfterOpenBracket](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
    style option.

- **Symbol naming conventions**. These are preferred in new code, but are not
required when doing so would need changes to significant pieces of existing
code.
  - Variable (including function arguments) and namespace names are all lowercase and may use `_` to
    separate words (snake_case).
    - Class member variables have a `m_` prefix.
    - Global variables have a `g_` prefix.
  - Constant names are all uppercase, and use `_` to separate words.
  - Enumerator constants may be `snake_case`, `PascalCase` or `ALL_CAPS`.
    This is a more tolerant policy than the [C++ Core
    Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Renum-caps),
    which recommend using `snake_case`.  Please use what seems appropriate.
  - Class names, function names, and method names are UpperCamelCase
    (PascalCase). Do not prefix class names with `C`.
  - Test suite naming convention: The Boost test suite in file
    `src/test/foo_tests.cpp` should be named `foo_tests`. Test suite names
    must be unique.

- **Miscellaneous**
  - `++i` is preferred over `i++`.
  - `nullptr` is preferred over `NULL` or `(void*)0`.
  - `static_assert` is preferred over `assert` where possible. Generally; compile-time checking is preferred over run-time checking.

Block style example:
```c++
int g_count = 0;

namespace foo {
class Class
{
    std::string m_name;

public:
    bool Function(const std::string& s, int n)
    {
        // Comment summarising what this section of code does
        for (int i = 0; i < n; ++i) {
            int total_sum = 0;
            // When something fails, return early
            if (!Something()) return false;
            ...
            if (SomethingElse(i)) {
                total_sum += ComputeSomething(g_count);
            } else {
                DoSomething(m_name, total_sum);
            }
        }

        // Success return is usually at the end
        return true;
    }
}
} // namespace foo
```

Coding Style (Python)
---------------------

Refer to [/test/functional/README.md#style-guidelines](/test/functional/README.md#style-guidelines).

Coding Style (Doxygen-compatible comments)
------------------------------------------

Bitcoin Core uses [Doxygen](https://www.doxygen.nl/) to generate its official documentation.

Use Doxygen-compatible comment blocks for functions, methods, and fields.

For example, to describe a function use:

```c++
/**
 * ... Description ...
 *
 * @param[in]  arg1 input description...
 * @param[in]  arg2 input description...
 * @param[out] arg3 output description...
 * @return Return cases...
 * @throws Error type and cases...
 * @pre  Pre-condition for function...
 * @post Post-condition for function...
 */
bool function(int arg1, const char *arg2, std::string& arg3)
```

A complete list of `@xxx` commands can be found at https://www.doxygen.nl/manual/commands.html.
As Doxygen recognizes the comments by the delimiters (`/**` and `*/` in this case), you don't
*need* to provide any commands for a comment to be valid; just a description text is fine.

To describe a class, use the same construct above the class definition:
```c++
/**
 * Alerts are for notifying old versions if they become too obsolete and
 * need to upgrade. The message is displayed in the status bar.
 * @see GetWarnings()
 */
class CAlert
```

To describe a member or variable use:
```c++
//! Description before the member
int var;
```

or
```c++
int var; //!< Description after the member
```

Also OK:
```c++
///
/// ... Description ...
///
bool function2(int arg1, const char *arg2)
```

Not picked up by Doxygen:
```c++
//
// ... Description ...
//
```

Also not picked up by Doxygen:
```c++
/*
 * ... Description ...
 */
```

A full list of comment syntaxes picked up by Doxygen can be found at https://www.doxygen.nl/manual/docblocks.html,
but the above styles are favored.

Recommendations:

- Avoiding duplicating type and input/output information in function
  descriptions.

- Use backticks (&#96;&#96;) to refer to `argument` names in function and
  parameter descriptions.

- Backticks aren't required when referring to functions Doxygen already knows
  about; it will build hyperlinks for these automatically. See
  https://www.doxygen.nl/manual/autolink.html for complete info.

- Avoid linking to external documentation; links can break.

- Javadoc and all valid Doxygen comments are stripped from Doxygen source code
  previews (`STRIP_CODE_COMMENTS = YES` in [Doxyfile.in](doc/Doxyfile.in)). If
  you want a comment to be preserved, it must instead use `//` or `/* */`.

### Generating Documentation

The documentation can be generated with `make docs` and cleaned up with `make
clean-docs`. The resulting files are located in `doc/doxygen/html`; open
`index.html` in that directory to view the homepage.

Before running `make docs`, you'll need to install these dependencies:

Linux: `sudo apt install doxygen graphviz`

MacOS: `brew install doxygen graphviz`

Development tips and tricks
---------------------------

### Compiling for debugging

Run configure with `--enable-debug` to add additional compiler flags that
produce better debugging builds.

### Show sources in debugging

If you have ccache enabled, absolute paths are stripped from debug information
with the -fdebug-prefix-map and -fmacro-prefix-map options (if supported by the
compiler). This might break source file detection in case you move binaries
after compilation, debug from the directory other than the project root or use
an IDE that only supports absolute paths for debugging.

There are a few possible fixes:

1. Configure source file mapping.

For `gdb` create or append to `.gdbinit` file:
```
set substitute-path ./src /path/to/project/root/src
```

For `lldb` create or append to `.lldbinit` file:
```
settings set target.source-map ./src /path/to/project/root/src
```

2. Add a symlink to the `./src` directory:
```
ln -s /path/to/project/root/src src
```

3. Use `debugedit` to modify debug information in the binary.

### Compiling for gprof profiling

Run configure with the `--enable-gprof` option, then make.

### `debug.log`

If the code is behaving strangely, take a look in the `debug.log` file in the data directory;
error and debugging messages are written there.

The `-debug=...` command-line option controls debugging; running with just `-debug` or `-debug=1` will turn
on all categories (and give you a very large `debug.log` file).

The Qt code routes `qDebug()` output to `debug.log` under category "qt": run with `-debug=qt`
to see it.

### Signet, testnet, and regtest modes

If you are testing multi-machine code that needs to operate across the internet,
you can run with either the `-signet` or the `-testnet` config option to test
with "play bitcoins" on a test network.

If you are testing something that can run on one machine, run with the
`-regtest` option.  In regression test mode, blocks can be created on demand;
see [test/functional/](/test/functional) for tests that run in `-regtest` mode.

### DEBUG_LOCKORDER

Bitcoin Core is a multi-threaded application, and deadlocks or other
multi-threading bugs can be very difficult to track down. The `--enable-debug`
configure option adds `-DDEBUG_LOCKORDER` to the compiler flags. This inserts
run-time checks to keep track of which locks are held and adds warnings to the
`debug.log` file if inconsistencies are detected.

### Assertions and Checks

The util file `src/util/check.h` offers helpers to protect against coding and
internal logic bugs. They must never be used to validate user, network or any
other input.

* `assert` or `Assert` should be used to document assumptions when any
  violation would mean that it is not safe to continue program execution. The
  code is always compiled with assertions enabled.
   - For example, a nullptr dereference or any other logic bug in validation
     code means the program code is faulty and must terminate immediately.
* `CHECK_NONFATAL` should be used for recoverable internal logic bugs. On
  failure, it will throw an exception, which can be caught to recover from the
  error.
   - For example, a nullptr dereference or any other logic bug in RPC code
     means that the RPC code is faulty and can not be executed. However, the
     logic bug can be shown to the user and the program can continue to run.
* `Assume` should be used to document assumptions when program execution can
  safely continue even if the assumption is violated. In debug builds it
  behaves like `Assert`/`assert` to notify developers and testers about
  nonfatal errors. In production it doesn't warn or log anything, though the
  expression is always evaluated.
   - For example it can be assumed that a variable is only initialized once,
     but a failed assumption does not result in a fatal bug. A failed
     assumption may or may not result in a slightly degraded user experience,
     but it is safe to continue program execution.

### Valgrind suppressions file

Valgrind is a programming tool for memory debugging, memory leak detection, and
profiling. The repo contains a Valgrind suppressions file
([`valgrind.supp`](https://github.com/bitcoin/bitcoin/blob/master/contrib/valgrind.supp))
which includes known Valgrind warnings in our dependencies that cannot be fixed
in-tree. Example use:

```shell
$ valgrind --suppressions=contrib/valgrind.supp src/test/test_bitcoin
$ valgrind --suppressions=contrib/valgrind.supp --leak-check=full \
      --show-leak-kinds=all src/test/test_bitcoin --log_level=test_suite
$ valgrind -v --leak-check=full src/bitcoind -printtoconsole
$ ./test/functional/test_runner.py --valgrind
```

### Compiling for test coverage

LCOV can be used to generate a test coverage report based upon `make check`
execution. LCOV must be installed on your system (e.g. the `lcov` package
on Debian/Ubuntu).

To enable LCOV report generation during test runs:

```shell
./configure --enable-lcov
make
make cov

# A coverage report will now be accessible at `./test_bitcoin.coverage/index.html`.
```

### Performance profiling with perf

Profiling is a good way to get a precise idea of where time is being spent in
code. One tool for doing profiling on Linux platforms is called
[`perf`](https://www.brendangregg.com/perf.html), and has been integrated into
the functional test framework. Perf can observe a running process and sample
(at some frequency) where its execution is.

Perf installation is contingent on which kernel version you're running; see
[this thread](https://askubuntu.com/questions/50145/how-to-install-perf-monitoring-tool)
for specific instructions.

Certain kernel parameters may need to be set for perf to be able to inspect the
running process's stack.

```sh
$ sudo sysctl -w kernel.perf_event_paranoid=-1
$ sudo sysctl -w kernel.kptr_restrict=0
```

Make sure you [understand the security
trade-offs](https://lwn.net/Articles/420403/) of setting these kernel
parameters.

To profile a running bitcoind process for 60 seconds, you could use an
invocation of `perf record` like this:

```sh
$ perf record \
    -g --call-graph dwarf --per-thread -F 140 \
    -p `pgrep bitcoind` -- sleep 60
```

You could then analyze the results by running:

```sh
perf report --stdio | c++filt | less
```

or using a graphical tool like [Hotspot](https://github.com/KDAB/hotspot).

See the functional test documentation for how to invoke perf within tests.


### Sanitizers

Bitcoin Core can be compiled with various "sanitizers" enabled, which add
instrumentation for issues regarding things like memory safety, thread race
conditions, or undefined behavior. This is controlled with the
`--with-sanitizers` configure flag, which should be a comma separated list of
sanitizers to enable. The sanitizer list should correspond to supported
`-fsanitize=` options in your compiler. These sanitizers have runtime overhead,
so they are most useful when testing changes or producing debugging builds.

Some examples:

```bash
# Enable both the address sanitizer and the undefined behavior sanitizer
./configure --with-sanitizers=address,undefined

# Enable the thread sanitizer
./configure --with-sanitizers=thread
```

If you are compiling with G