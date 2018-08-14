/** @file
  Test file for DBHost
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

#include "catch.hpp"

#include "DBTable.h"
#include "DBHost.h"
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace std;

std::size_t
Hash32fnv(std::string_view const &s)
{
  std::size_t hval = 0;
  for (const char &c : s) {
    hval *= 0x01000193;
    hval ^= (std::size_t)c;
  }
  return hval;
}

std::size_t
Hash32fnv(std::string const &s)
{
  std::size_t hval = 0;
  for (const char &c : s) {
    hval *= 0x01000193;
    hval ^= (std::size_t)c;
  }
  return hval;
}

TEST_CASE("DBTable <int,int>", "[DBTable] [constructor]")
{
  auto db_test       = DBTable<int, int>(2);
  *db_test.obtain(4) = 4;
}
TEST_CASE("DBTable <sting,int>", "[DBTable] [constructor]")
{
  auto db_test                 = DBTable<string, int>(2);
  *(db_test.obtain({"hello"})) = 1;
  *(db_test.obtain({"world"})) = 2;
}
TEST_CASE("DBTable <string,int,CustomHasher>", "[DBTable] [constructor]")
{
  auto db_test                 = DBTable<string, int, CustomHasher<string, &Hash32fnv>>(2);
  *(db_test.obtain({"hello"})) = 1;
  *(db_test.obtain({"world"})) = 2;
}
TEST_CASE("DBTable <string_view,int>", "[DBTable] [constructor]")
{
  auto db_test                 = DBTable<string_view, int>(2);
  *(db_test.obtain({"hello"})) = 1;
  *(db_test.obtain({"world"})) = 2;
}

TEST_CASE("DBTable <string_view,int,CustomHasher>", "[DBTable] [constructor]")
{
  auto db_test                 = DBTable<string_view, int, CustomHasher<string_view, &Hash32fnv>>(2);
  *(db_test.obtain({"hello"})) = 1;
  *(db_test.obtain({"world"})) = 2;
}
/*
template <typename TableType, typename KeyType>
uint64_t
time_trial(std::vector<KeyType> const &indexes)
{
  TableType table(64);
  int ref;
  for (const auto idx_str : indexes) {
    table.obtain({idx_str}) = 1;
  }
  auto start = std::chrono::system_clock::now();
  for (const auto idx_str : indexes) {
    table.find({idx_str}, ref);
  }
  auto end = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
};


// Conclusion of multiple performance tests:
// * where baseline is keyType std::string and std::hash
// 1. wrapping a key with a key&hash pair slightly improves performance by 7% when hash is precomputed and hurts performance by 14%
when not pre-hashed. The bad out weighs the good and it convolutes code.
// 2. keying on string_view is 5% faster than string
// 3. CustomHasher has equivalent performance to std::hash
TEST_CASE("CustomHasher time test", "")
{
  // max thread priority
  struct sched_param params;
  params.sched_priority = sched_get_priority_max(SCHED_FIFO);
  pthread_setschedparam(pthread_self(), SCHED_FIFO, &params);

  using TypeA = PartitionedMap<string, int>;
  using TypeB = PartitionedMap<string, int, CustomHasher<string, &Hash32fnv>>;
  using TypeC = PartitionedMap<string_view, int, CustomHasher<string_view, &Hash32fnv>>;
  using TypeD = PartitionedMap<string_view, int>;

  std::vector<TypeA::KeyType> indexes;
  std::vector<TypeB::KeyType> indexesHashed;
  char buf[2000];
  const int data_len = 10000;
  const int rounds   = 1000;

  for (int i = 0; i < data_len; i++) {
    sprintf(buf, "%d", i * i);
    indexes.push_back({buf});
    indexesHashed.push_back({buf});
  }

  std::vector<double> time_a, time_b, time_b1, time_c, time_d;

  for (int round = 0; round < rounds; round++) {
    time_a.push_back(time_trial<TypeA, TypeA::KeyType>(indexes) / data_len);
    time_b.push_back(time_trial<TypeB, TypeB::KeyType>(indexesHashed) / data_len);
    time_b1.push_back(time_trial<TypeB, string>(indexes) / data_len);
    time_c.push_back(time_trial<TypeC, string>(indexes) / data_len);
    time_d.push_back(time_trial<TypeD, string>(indexes) / data_len);
  }

  auto median = [](std::vector<double> &v) {
    std::nth_element(v.begin(), v.begin() + v.size() * 9 / 10, v.end());
    return v[v.size() * 9 / 10];
  };

  printf("A:%f B:%f b:%f C:%f D:%f", median(time_a), median(time_b), median(time_b1), median(time_c), median(time_d));
  CHECK(0);
}*/

typename DBHost::BitFieldId bit_a, bit_b;
shared_ptr<DBHost> host_ptr, host_ptr2;

// ======= test for DBHost ========

TEST_CASE("DBHost constructor", "[NextHop] [DBHost] [constructor]")
{
  typename DBHost::KeyType fqdn_1{"test_host.com"};

  INFO("Declare fields")
  {
    REQUIRE(DBHost::schema.addField(bit_a, "bit_a"));
    REQUIRE(DBHost::schema.addField(bit_b, "bit_b"));
  }

  INFO("obtain")
  {
    host_ptr = DBHost::table.obtain(fqdn_1);
    REQUIRE(host_ptr);
    host_ptr2 = DBHost::table.obtain({"test_host.com"});
    REQUIRE(host_ptr2);
    REQUIRE(host_ptr.get() == host_ptr2.get()); // point to same instance
  }

  INFO("find")
  {
    //
    host_ptr2.reset();
    REQUIRE(DBHost::table.find(fqdn_1, host_ptr2) == true);
    REQUIRE(host_ptr.get() == host_ptr2.get()); // point to same instance
    REQUIRE(DBHost::table.find({"fail_host.com"}, host_ptr2) == false);
  }

  INFO("use fields")
  {
    auto &host = *host_ptr;
    host.writeBit(bit_a, 1);
    REQUIRE(host[bit_a] == 1);
    REQUIRE(host[bit_b] == 0);
    host.writeBit(bit_b, 1);
    host.writeBit(bit_a, 0);
    REQUIRE(host[bit_a] == 0);
    REQUIRE(host[bit_b] == 1);
  }

  INFO("pop")
  {
    //
    REQUIRE(DBHost::table.pop(fqdn_1) == host_ptr);
    REQUIRE(DBHost::table.find(fqdn_1, host_ptr2) == false);
    // shared_ptr retain data while in scope
    REQUIRE(host_ptr);
    host_ptr.reset();
    host_ptr2.reset();
  }

  INFO("end DBHost Tests");
}
