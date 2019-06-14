#!/usr/bin/env python3
# Copyright (c) 2018-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import sys
import re
from typing import Dict, List, Set

MAPPING = {
    'core_read.cpp': 'core_io.cpp',
    'core_write.cpp': 'core_io.cpp',
}

# Directories with header-based modules, where the assumption that .cpp files
# define functions and variables declared in corresponding .h files is
# incorrect.
HEADER_MODULE_PATHS = [
    'interfaces/'
]

def module_name(path):
    if path in MAPPING:
        path = MAPPING[path]
    if any(path.startswith(dirpath) for dirpath in HEADER_MODULE_PATHS):
        return path
    if path.endswith(".h"):
        return path[:-2]
    if path.endswith(".c"):
        return path[:-2]
    if path.endswith(".cpp"):
        return path[:-4]
    return None

files = dict()
deps: Dict[str, Set[str]] = dict()

RE = re.compile("^#include <(.*)>")

# Iterate over files, and create list of modules
for arg in sys.argv[1:]:
    module = module_name(arg)
    if module is None:
        print("Ignoring file %s (does not constitute module)\n" % arg)
    else:
        files[arg] = module
        deps[module] = set()

# Iterate again, and build list of direct dependencies for each module
# TODO: implement support for multiple include directories
for arg in sorted(files.keys()):
    module = files[arg]
    with open(arg, 'r', encoding="utf8") as f:
        for line in f:
            match = RE.match(line)
            if match:
                include = match.group(1)
                included_module = module_name(include)
                if included_module is not None and included_module in deps and included_module != module:
                    deps[module].add(included_module)

# Loop to find the shortest (remaining) circular dependency
have_cycle: bool = False
while True:
    shortest_cycle = None
    for module in sorted(deps.keys()):
        # Build the transitive closure of dependencies of module
