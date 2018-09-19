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

   test_arena.cc

   Description:

   A short test program intended to be used with Purify to detect problems
   with the arnea code


 ****************************************************************************/

#include "catch.hpp"

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

TEST_CASE("test arena", "[libts][arena]")
{
  const int sizes_to_test   = 12;
  const int regions_to_test = 1024 * 2;
  char **test_regions       = new char *[regions_to_test];
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
      char a = 'a' + (j % 52);
      for (int k = 0; k < test_size; k++) {
        REQUIRE(test_regions[j][k] == a);
        a = (a + 1) % 52;
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
}
