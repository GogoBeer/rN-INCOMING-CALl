/**********************************************************************
 * Copyright (c) 2018 Pieter Wuille, Greg Maxwell, Gleb Naumenko      *
 * Distributed under the MIT software license, see the accompanying   *
 * file LICENSE or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include "../include/minisketch.h"

int main(void) {

  minisketch *sketch_a = minisketch_create(12, 0, 4);

  for (int i = 3000; i < 3010; ++i) {
    minisketch_add_uint64(sketch_a, i);
  }

  size_t sersize = minisketch_serialized_size(sketch_a);
  assert(sersize == 12 * 4 / 8); // 4 12-bit values is 6 bytes.
  unsigned char *buffer_a = malloc(sersize);
  minisketch_serialize(