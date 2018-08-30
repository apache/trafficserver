/** @file

  Test code for the Map templates.

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
#include <cstdint>
#include "tscore/ink_platform.h"
#include "P_EventSystem.h"
#include "tscore/MT_hashtable.h"
#include "I_Lock.h"
#include <catch.hpp>

// -------------
// --- TESTS ---
// -------------
TEST_CASE("MT_hashtable", "[libts][MT_hashtable]")
{
  MTHashTable<long, long> *htable = new MTHashTable<long, long>(4);
  // add elements to the table;
  long i, count = 1 * 1024 * 1024;
  bool check_failed = false;

  for (i = 1; i <= count; i++) {
    htable->insert_entry(i, i);
  }

  INFO("verifying the content");
  for (i = 1; i <= count; i++) {
    long data = htable->lookup_entry(i);

    if (data != i) {
      check_failed = true;
    }
  }
  CHECK(check_failed == false);

  long removed_count = 0;
  // delete some data
  INFO("removing data.");
  for (i = 1; i <= count / 2; i++) {
    htable->remove_entry(i * 2);
    removed_count++;
  }

  INFO("data entries are removed");
  CHECK(removed_count == count / 2);

  INFO("verify the content again");
  check_failed = false;
  for (i = 1; i <= count; i++) {
    long data = htable->lookup_entry(i);
    if (i % 2 == 1 && data == 0) {
      check_failed = true;
    }
    if (data != 0 && data != i) {
      check_failed = true;
    }
  }
  CHECK(check_failed == false);

  INFO("use iterator to list all the elements and delete half of them");
  HashTableIteratorState<long, long> it;
  int j, new_count = 0;
  for (j = 0; j < MT_HASHTABLE_PARTITIONS; j++) {
    int data = htable->first_entry(j, &it);
    while (data > 0) {
      new_count++;

      if (new_count % 2 == 0) {
        htable->remove_entry(j, &it);
        data = htable->cur_entry(j, &it);
        removed_count++;
      } else {
        data = htable->next_entry(j, &it);
      }
    }
  }

  INFO("verify the content once again");
  check_failed = false;
  new_count    = count - removed_count;
  for (j = 0; j < MT_HASHTABLE_PARTITIONS; j++) {
    int data = htable->first_entry(j, &it);
    while (data > 0) {
      new_count--;

      data = htable->next_entry(j, &it);
      if (data != htable->lookup_entry(data)) {
        check_failed = true;
      }
    }
  }
  CHECK(check_failed == false);

  INFO("there are no extra entries in the table");
  check_failed = false;
  if (new_count != 0) {
    check_failed = true;
  }
  CHECK(check_failed == false);

  INFO("remove everything using iterator");
  check_failed = false;
  new_count    = count - removed_count;
  for (j = 0; j < MT_HASHTABLE_PARTITIONS; j++) {
    int data = htable->first_entry(j, &it);
    while (data > 0) {
      new_count--;
      htable->remove_entry(j, &it);
      data = htable->cur_entry(j, &it);
    }
  }

  if (new_count != 0) {
    check_failed = true;
  }
  CHECK(check_failed == false);

  delete htable;
}
