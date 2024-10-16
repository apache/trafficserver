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
#include "test_doubles.h"

#include "tscore/EventNotify.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

static void
init_disk(CacheDisk &disk)
{
  disk.path                = static_cast<char *>(ats_malloc(1));
  disk.path[0]             = '\0';
  disk.disk_stripes        = static_cast<DiskStripe **>(ats_malloc(sizeof(DiskStripe *)));
  disk.disk_stripes[0]     = nullptr;
  disk.header              = static_cast<DiskHeader *>(ats_malloc(sizeof(DiskHeader)));
  disk.header->num_volumes = 0;
}

/* Catch test helper to provide a StripeSM with a valid file descriptor.
 *
 * The file will be deleted automatically when the application ends normally.
 * If the StripeSM already has a valid file descriptor, that file will NOT be
 * closed.
 *
 * @param stripe: A StripeSM object with no valid file descriptor.
 * @return The std::FILE* stream if successful, otherwise the Catch test will
 *   be failed at the point of error.
 */
static std::FILE *
attach_tmpfile_to_stripe(StripeSM &stripe)
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
static std::FILE *
init_stripe_for_writing(StripeSM &stripe, StripteHeaderFooter &header, CacheVol &cache_vol)
{
  stripe.cache_vol                             = &cache_vol;
  cache_rsb.write_bytes                        = Metrics::Counter::createPtr("unit_test.write.bytes");
  stripe.cache_vol->vol_rsb.write_bytes        = Metrics::Counter::createPtr("unit_test.write.bytes");
  cache_rsb.gc_frags_evacuated                 = Metrics::Counter::createPtr("unit_test.gc.frags.evacuated");
  stripe.cache_vol->vol_rsb.gc_frags_evacuated = Metrics::Counter::createPtr("unit_test.gc.frags.evacuated");

  stripe.sector_size = 256;

  // This is the minimum value for header.write_pos. Offsets are calculated
  // based on the distance of the write_pos from this point. If we ever move
  // the write head before the start of the stripe data section, we will
  // underflow offset calculations and end up in big trouble.
  header.write_pos = stripe.start;
  header.agg_pos   = 1;
  header.phase     = 0;
  stripe.header    = &header;
  return attach_tmpfile_to_stripe(stripe);
}

TEST_CASE("The behavior of StripeSM::add_writer.")
{
  FakeVC    vc;
  CacheDisk disk;
  init_disk(disk);
  StripeSM stripe{&disk, 10, 0};

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

  ats_free(stripe.raw_dir);
  ats_free(stripe.get_preserved_dirs().evacuate);
  ats_free(stripe.path);
}

// This test case demonstrates how to set up a StripeSM and make
// a call to aggWrite without causing memory errors. It uses a
// tmpfile for the StripeSM to write to.
TEST_CASE("aggWrite behavior with f.evacuator unset")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM            stripe{&disk, 10, 0};
  StripteHeaderFooter header;
  CacheVol            cache_vol;
  auto               *file{init_stripe_for_writing(stripe, header, cache_vol)};
  WaitingVC           vc{&stripe};
  char const         *source = "yay";
  vc.set_test_data(source, 4);
  vc.set_write_len(4);
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

  SECTION("Given the aggregation buffer is partially full, sync is set, "
          "and checksums are enabled, "
          "when we schedule aggWrite with a VC buffer containing 'yay', ")
  {
    cache_config_enable_checksum = true;
    vc.f.sync                    = 1;
    vc.f.use_first_key           = 1;
    vc.write_serial              = 1;
    header.write_serial          = 10;
    int document_offset          = header.write_pos;
    {
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      stripe.aggWrite(EVENT_NONE, 0);
    }
    vc.wait_for_callback();

    SECTION("then some bytes should be written to disk")
    {
      CHECK(0 < header.agg_pos);
    }

    Doc doc;

    std::size_t documents_read{};
    {
      // Other threads are still alive and may interact with the
      // cache periodically, so we should always grab the lock
      // whenever we interact with the underlying file.
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      fseek(file, document_offset, SEEK_SET);
      documents_read = fread(&doc, sizeof(Doc), 1, file);
    }
    REQUIRE(1 == documents_read);

    // An incorrect magic value would indicate that the document is corrupt.
    REQUIRE(DOC_MAGIC == doc.magic);

    SECTION("then the document should be single fragment.")
    {
      CHECK(doc.single_fragment());
    }

    SECTION("then the document header length should be 0.")
    {
      CHECK(0 == doc.hlen);
    }

    SECTION("then the document length should be sizeof(Doc) + 4.")
    {
      CHECK(sizeof(Doc) + 4 == doc.len);
    }

    SECTION("then the document checksum should be correct.")
    {
      std::uint32_t expected = 'y' + 'a' + 'y' + '\0';
      CHECK(expected == doc.checksum);
    }

    SECTION("then the document data should contain 'yay'.")
    {
      std::size_t data_buffers_read{};
      char       *data = new char[doc.data_len()];
      {
        SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
        fseek(file, document_offset + doc.prefix_len(), SEEK_SET);
        data_buffers_read = fread(data, doc.data_len(), 1, file);
      }
      REQUIRE(1 == data_buffers_read);
      INFO("buffer content is \"" << data << "\"");
      CHECK(0 == strncmp("yay", data, 3));
      delete[] data;
    }

    cache_config_enable_checksum = false;
  }

  ats_free(stripe.raw_dir);
  ats_free(stripe.get_preserved_dirs().evacuate);
  ats_free(stripe.path);
}

// When f.evacuator is set, vc.buf must contain a Doc object including headers
// and data.
// We don't use a CacheEvacuateDocVC because the behavior under test depends
// only on the presence of the f.evacuator flag.
TEST_CASE("aggWrite behavior with f.evacuator set")
{
  CacheDisk disk;
  init_disk(disk);
  StripeSM            stripe{&disk, 10, 0};
  StripteHeaderFooter header;
  CacheVol            cache_vol;
  auto               *file{init_stripe_for_writing(stripe, header, cache_vol)};
  WaitingVC           vc{&stripe};
  char               *source = new char[sizeof(Doc) + 4]{};
  const char         *yay    = "yay";
  Doc                 doc{};
  doc.magic     = DOC_MAGIC;
  doc.len       = sizeof(Doc) + 4;
  doc.total_len = 4;
  doc.hlen      = 0;
  std::memcpy(source, &doc, sizeof(doc));
  std::memcpy(source + sizeof(Doc), yay, 4);
  vc.set_test_data(source, sizeof(Doc) + 4);
  vc.set_write_len(stripe.round_to_approx_size(doc.len));
  vc.set_agg_len(stripe.round_to_approx_size(vc.write_len + vc.header_len + vc.frag_len));
  vc.mark_as_evacuator();
  stripe.add_writer(&vc);

  SECTION("Given the aggregation buffer is partially full, sync is set, "
          "when we schedule aggWrite with a VC buffer containing 'yay', ")
  {
    vc.f.sync           = 1;
    vc.f.use_first_key  = 1;
    vc.write_serial     = 1;
    header.write_serial = 10;
    int document_offset = header.write_pos;
    {
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      stripe.aggWrite(EVENT_NONE, 0);
    }
    vc.wait_for_callback();

    SECTION("then some bytes should be written to disk")
    {
      CHECK(0 < header.agg_pos);
    }

    Doc doc;

    std::size_t documents_read{};
    {
      // Other threads are still alive and may interact with the
      // cache periodically, so we should always grab the lock
      // whenever we interact with the underlying file.
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      fseek(file, document_offset, SEEK_SET);
      documents_read = fread(&doc, sizeof(Doc), 1, file);
    }
    REQUIRE(1 == documents_read);

    // An incorrect magic value would indicate that the document is corrupt.
    REQUIRE(DOC_MAGIC == doc.magic);

    SECTION("then the document should be single fragment.")
    {
      CHECK(doc.single_fragment());
    }

    SECTION("then the document header length should be 0.")
    {
      CHECK(0 == doc.hlen);
    }

    SECTION("then the document length should be sizeof(Doc) + 4.")
    {
      CHECK(sizeof(Doc) + 4 == doc.len);
    }

    SECTION("then the document data should contain 'yay'.")
    {
      std::size_t data_buffers_read{};
      char       *data = new char[doc.data_len()];
      {
        SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
        fseek(file, document_offset + doc.prefix_len(), SEEK_SET);
        data_buffers_read = fread(data, doc.data_len(), 1, file);
      }
      REQUIRE(1 == data_buffers_read);
      INFO("buffer content is \"" << data << "\"");
      CHECK(0 == strncmp("yay", data, 3));
      delete[] data;
    }
  }

  delete[] source;
  ats_free(stripe.raw_dir);
  ats_free(stripe.get_preserved_dirs().evacuate);
  ats_free(stripe.path);
}
