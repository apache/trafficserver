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

#include "tscore/EventNotify.h"

#include <array>
#include <cstdio>
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

class FakeVC : public CacheVC
{
public:
  FakeVC()
  {
    this->buf = new_IOBufferData(iobuffer_size_to_index(1024, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
    SET_HANDLER(&FakeVC::handle_call);
  }

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

  int
  handle_call(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
  {
    return EVENT_CONT;
  }
};

class WaitingVC final : public FakeVC
{
public:
  WaitingVC(Stripe *stripe)
  {
    SET_HANDLER(&WaitingVC::handle_call);
    this->stripe = stripe;
    this->dir    = *stripe->dir;
  }

  void
  wait_for_callback()
  {
    this->_notifier.lock();
    while (!this->_got_callback) {
      this->_notifier.wait();
    }
    this->_notifier.unlock();
  }

  int
  handle_call(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
  {
    this->_got_callback = true;
    this->_notifier.signal();
    return EVENT_CONT;
  }

private:
  EventNotify _notifier;
  bool        _got_callback{false};
};

/* Catch test helper to provide a Stripe with a valid file descriptor.
 *
 * The file will be deleted automatically when the application ends normally.
 * If the Stripe already has a valid file descriptor, that file will NOT be
 * closed.
 *
 * @param stripe: A Stripe object with no valid file descriptor.
 * @return The std::FILE* stream if successful, otherwise the Catch test will
 *   be failed at the point of error.
 */
static std::FILE *
attach_tmpfile_to_stripe(Stripe &stripe)
{
  auto *file{std::tmpfile()};
  REQUIRE(file != nullptr);
  int fd{fileno(file)};
  REQUIRE(fd != -1);
  stripe.fd = fd;
  return file;
}

// We can't return a stripe from this function because the copy
// and move constructors are deleted.
static void
init_stripe_for_writing(Stripe &stripe, StripteHeaderFooter &header, CacheVol &cache_vol)
{
  stripe.cache_vol                                = &cache_vol;
  cache_rsb.write_backlog_failure                 = Metrics::Counter::createPtr("unit_test.write.backlog.failure");
  stripe.cache_vol->vol_rsb.write_backlog_failure = Metrics::Counter::createPtr("unit_test.write.backlog.failure");

  // A number of things must be initialized in a certain way for Stripe
  // not to segfault, hit an assertion, or exhibit zero-division.
  // I just picked values that happen to work.
  stripe.sector_size = 256;
  stripe.skip        = 0;
  stripe.len         = 600000000000000;
  stripe.segments    = 1;
  stripe.buckets     = 4;
  stripe.start       = stripe.skip + 2 * stripe.dirlen();
  stripe.raw_dir     = static_cast<char *>(ats_memalign(ats_pagesize(), stripe.dirlen()));
  stripe.dir         = reinterpret_cast<Dir *>(stripe.raw_dir + stripe.headerlen());

  stripe.evacuate = static_cast<DLL<EvacuationBlock> *>(ats_malloc(2024));
  memset(static_cast<void *>(stripe.evacuate), 0, 2024);

  header.write_pos = 50000;
  header.agg_pos   = 1;
  stripe.header    = &header;
  attach_tmpfile_to_stripe(stripe);
}

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

// This test case demonstrates how to set up a Stripe and make
// a call to aggWrite without causing memory errors. It uses a
// tmpfile for the Stripe to write to.
TEST_CASE("aggWrite behavior")
{
  Stripe              stripe;
  StripteHeaderFooter header;
  CacheVol            cache_vol;
  init_stripe_for_writing(stripe, header, cache_vol);
  WaitingVC vc{&stripe};
  vc.set_write_len(1);
  vc.set_agg_len(stripe.round_to_approx_size(vc.write_len + vc.header_len + vc.frag_len));
  stripe.add_writer(&vc);

  SECTION("Given the aggregation buffer is only partially full and no sync, "
          "when we call aggWrite, "
          "then nothing should be written to disk.")
  {
    header.agg_pos = 0;
    {
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      stripe.aggWrite(EVENT_NONE, 0);
    }
    vc.wait_for_callback();
    CHECK(0 == header.agg_pos);
  }

  SECTION("Given the aggregation buffer is partially full and sync is set, "
          "when we schedule aggWrite, "
          "then some bytes should be written to disk.")
  {
    vc.f.sync           = 1;
    vc.f.use_first_key  = 1;
    vc.write_serial     = 1;
    header.write_serial = 10;
    {
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      stripe.aggWrite(EVENT_NONE, 0);
    }
    vc.wait_for_callback();
    // We don't check here what bytes were written. In fact according
    // to valgrind it's writing uninitialized parts of the aggregation
    // buffer, but that's OK because in this SECTION we only care that
    // something was written successfully without anything blowing up.
    CHECK(0 < header.agg_pos);
  }

  ats_free(stripe.raw_dir);
  ats_free(stripe.evacuate);
}
