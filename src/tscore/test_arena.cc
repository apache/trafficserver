/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

   ArenaTest.cc

   Description:

   A short test program intended to be used with Purify to detect problems
   with the arnea code


 ****************************************************************************/

#include "tscore/Arena.h"
#include <cstdio>

void
fill_test_data(char *ptr, int size, int seed)
{
  char a = 'a' + (seed % 52);

  for (int i = 0; i < size; i++) {
    ptr[i] = a;
    a      = (a + 1) % 52;
  }
}

int
check_test_data(char *ptr, int size, int seed)
{
  int fail = 0;
  char a   = 'a' + (seed % 52);

  for (int i = 0; i < size; i++) {
    if (ptr[i] != a) {
      fail++;
    }
    a = (a + 1) % 52;
  }

  return fail;
}

int
test_block_boundries()
{
  const int sizes_to_test   = 12;
  const int regions_to_test = 1024 * 2;
  char **test_regions       = new char *[regions_to_test];
  int failures              = 0;
  Arena *a                  = new Arena();

  for (int i = 0; i < sizes_to_test; i++) {
    int test_size = 1 << i;

    // Clear out the regions array
    int j = 0;
    for (j = 0; j < regions_to_test; j++) {
      test_regions[j] = nullptr;
    }

    // Allocate and fill the array
    for (j = 0; j < regions_to_test; j++) {
      test_regions[j] = (char *)a->alloc(test_size);
      fill_test_data(test_regions[j], test_size, j);
    }

    // Now check to make sure the data is correct
    for (j = 0; j < regions_to_test; j++) {
      int f = check_test_data(test_regions[j], test_size, j);

      if (f != 0) {
        fprintf(stderr, "block_boundries test failed.  size %d region %d\n", test_size, j);
        failures++;
      }
    }

    // Now free the regions
    for (j = 0; j < regions_to_test; j++) {
      a->free(test_regions[j], test_size);
    }

    a->reset();
  }

  delete[] test_regions;
  delete a;
  return failures;
}

int
main()
{
  int failures = 0;

  failures += test_block_boundries();

  if (failures) {
    return 1;
  } else {
    return 0;
  }
}
