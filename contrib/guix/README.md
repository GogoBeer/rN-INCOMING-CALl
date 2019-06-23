# Bootstrappable Bitcoin Core Builds

This directory contains the files necessary to perform bootstrappable Bitcoin
Core builds.

[Bootstrappability][b17e] furthers our binary security guarantees by allowing us
to _audit and reproduce_ our toolchain instead of blindly _trusting_ binary
downloads.

We achieve bootstrappability by using Guix as a functional package manager.

# Requirements

Conservatively, you will need an x86_64 machine with:

- 16GB of free disk space on the partition that /gnu/store will reside in
- 8GB of free disk space **per platform triple** you're planning on building
  (see the `HOSTS` [environment variable description][env-vars-list])

# Installation and Setup

If you don't have Guix installed and set up, please follow the instructions