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

#include "main.h"

#include <array>
#include <ostream>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

struct AddWriterBranchTest {
  int  initial_buffer_size{};
  int  agg_len{};
  int  header_len{};
  int  write_len{};
  int  readers{};
  bool result{};
};

std::array<AddWriterBranchTest, 32> add_writer_branch_test_cases = {
  {
   {0, 0, 0, 0, 0, true},
   {0, 0, 0, 0, 1, true},
   {0, 0, 0, 1, 0, true},
   {0, 0, 0, 1, 1, true},
   {0, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 0, false},
   {0, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 1, false},
   {0, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 0, false},
   {0, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 1, false},
   {0, AGG_SIZE + 1, 0, 0, 0, false},
   {0, AGG_SIZE + 1, 0, 0, 1, false},
   {0, AGG_SIZE + 1, 0, 1, 0, false},
   {0, AGG_SIZE + 1, 0, 1, 1, false},
   {0, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 0, false},
   {0, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 1, false},
   {0, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 0, false},
   {0, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 1, false},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, 0, 0, 0, true},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, 0, 0, 1, true},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, 0, 1, 0, false},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, 0, 1, 1, true},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 0, false},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 1, false},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 0, false},
   {AGG_SIZE + cache_config_agg_write_backlog, 0, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 1, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, 0, 0, 0, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, 0, 0, 1, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, 0, 1, 0, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, 0, 1, 1, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 0, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 0, 1, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 0, false},
   {AGG_SIZE + cache_config_agg_write_backlog, AGG_SIZE + 1, MAX_FRAG_SIZE + 1 - sizeof(Doc), 1, 1, false},
   }
};

class FakeVC final : public CacheVC
{
public:
  void
  set_agg_len(int agg_len)
  {
    this->agg_len = agg_len;
  }

  void
  set_header_len(int header_len)
  {
    this->header_len = header_len;
  }

  void
  set_write_len(int write_len)
  {
    this->write_len = write_len;
  }

  void
  set_readers(int readers)
  {
    this->f.readers = readers;
  }
};

TEST_CASE("The behavior of Stripe::add_writer.")
{
  FakeVC vc;
  Stripe stripe;

  SECTION("Branch tests.")
  {
    AddWriterBranchTest test_parameters = GENERATE(from_range(add_writer_branch_test_cases));
    INFO("Initial buffer size: " << test_parameters.initial_buffer_size);
    INFO("VC agg_len: " << test_parameters.agg_len);
    INFO("VC header length: " << test_parameters.header_len);
    INFO("VC write length: " << test_parameters.write_len);
    INFO("VC readers: " << test_parameters.readers);
    INFO("Expected result: " << (test_parameters.result ? "true" : "false"));
    vc.set_agg_len(AGG_SIZE);
    for (int pending = 0; pending <= test_parameters.initial_buffer_size; pending += AGG_SIZE) {
      stripe.add_writer(&vc);
    }
    vc.set_agg_len(test_parameters.agg_len);
    vc.set_write_len(test_parameters.write_len);
    vc.set_header_len(test_parameters.header_len);
    vc.set_readers(test_parameters.readers);
    bool result = stripe.add_writer(&vc);
    CHECK(test_parameters.result == result);
  }

  SECTION("Boundary cases.")
  {
    SECTION("agg_len")
    {
      vc.set_agg_len(AGG_SIZE);
      bool result = stripe.add_writer(&vc);
      CHECK(true == result);
    }

    SECTION("header_len")
    {
      vc.set_header_len(MAX_FRAG_SIZE - sizeof(Doc));
      bool result = stripe.add_writer(&vc);
      CHECK(true == result);
    }

    SECTION("initial pending bytes")
    {
      vc.set_agg_len(1);
      for (int pending = 0; pending < AGG_SIZE + cache_config_agg_write_backlog; pending += 1) {
        stripe.add_writer(&vc);
      }
      bool result = stripe.add_writer(&vc);
      CHECK(true == result);
    }
  }
}
