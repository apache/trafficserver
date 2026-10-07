/** @file

  Unit tests for the Doc sanity checks in CacheVC::handleReadDone.

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

#include "../P_CacheInternal.h"

#include <array>
#include <cstdint>
#include <cstring>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

// Larger than every read below, so a len claimed past the read still lands inside the allocation.
constexpr std::size_t BUFFER_SIZE = 8192;

void
init_stripe_for_reading(StripeSM &stripe, StripeHeaderFooter &header)
{
  header.write_pos        = stripe.start + 8 * CACHE_BLOCK_SIZE;
  header.agg_pos          = header.write_pos;
  header.phase            = 0;
  stripe.directory.header = &header;
}

Dir
dir_at_stripe_start()
{
  Dir dir;
  dir_clear(&dir);
  dir_set_offset(&dir, 1);
  dir_set_phase(&dir, 0);
  return dir;
}

void
init_vc_for_reading(FakeVC &vc, StripeSM &stripe, Dir const &dir, CacheKey &key)
{
  vc.mutex    = new_ProxyMutex();
  vc.stripe   = &stripe;
  vc.dir      = dir;
  vc.read_key = &key;
  vc.buf      = new_IOBufferData(iobuffer_size_to_index(BUFFER_SIZE, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  std::memset(vc.buf->data(), 0, BUFFER_SIZE);
  // Keeps the RAM cache, which this harness does not build, out of the path.
  vc.vio.op = VIO::NONE;
}

// Non-HTTP and unchecksummed, so handleReadDone never interprets the bytes after the Doc header.
Doc *
make_doc(FakeVC &vc, CacheKey const &key, uint32_t len, uint32_t hlen)
{
  Doc *doc       = reinterpret_cast<Doc *>(vc.buf->data());
  doc->magic     = DOC_MAGIC;
  doc->len       = len;
  doc->hlen      = hlen;
  doc->total_len = len - sizeof(Doc) - hlen;
  doc->first_key = key;
  doc->key       = key;
  doc->doc_type  = CACHE_FRAG_TYPE_NONE;
  doc->v_major   = CACHE_DB_MAJOR_VERSION;
  doc->v_minor   = CACHE_DB_MINOR_VERSION;
  doc->checksum  = DOC_NO_CHECKSUM;
  return doc;
}

// Stands in for do_read_call + a completed AIO: the pushed handler is what handleReadDone pops on the way out.
void
run_read_done(FakeVC &vc, std::size_t nbytes)
{
  vc.io.aiocb.aio_nbytes = nbytes;
  vc.io.aio_result       = static_cast<int64_t>(nbytes);
  vc.save_handler        = vc.handler;

  SCOPED_MUTEX_LOCK(lock, vc.mutex, this_ethread());
  vc.handleReadDone(AIO_EVENT_DONE, nullptr);
}

struct BoundaryCase {
  std::size_t nbytes;
  uint32_t    len;
  uint32_t    hlen;
  bool        corrupt;
};

// The production crash corrupted a 4K IOBuffer freelist. Approximate sizes are multiples of 512, so a 3584-byte read lands
// in the same 4K block as a 4096-byte one, and the block's unread tail must not count as read. Each read size also has a
// corrupt row, which proves the harness gets past the dir_valid/io.ok() gate for it. A len below sizeof(Doc) would wrap the
// hlen bound and let any hlen through.
constexpr std::array<BoundaryCase, 10> boundary_cases = {
  {
   {4096, sizeof(Doc) - 1, 0, true},
   {4096, sizeof(Doc), 0, false},
   {4096, sizeof(Doc) + 100, 0, false},
   {4096, 4096, 0, false},
   {4096, 4097, 0, true},
   {4096, 4096, 4096 - sizeof(Doc), false},
   {4096, 4096, 4096 - sizeof(Doc) + 1, true},
   {3584, 3584, 0, false},
   {3584, 3585, 0, true},
   {3584, 4096, 0, true},
   }
};

} // namespace

TEST_CASE("handleReadDone checks Doc lengths at the 4K boundary", "[cache][read]")
{
  BoundaryCase c = GENERATE(from_range(boundary_cases));
  INFO("read " << c.nbytes << " len " << c.len << " hlen " << c.hlen);

  CacheDisk disk;
  init_disk(disk);
  StripeSM           stripe{&disk, 10, 0};
  StripeHeaderFooter header{};
  init_stripe_for_reading(stripe, header);

  Dir      dir = dir_at_stripe_start();
  CacheKey key;
  key.b[0] = 0x1234;
  key.b[1] = 0x5678;
  REQUIRE(stripe.dir_valid(&dir));

  FakeVC vc;
  init_vc_for_reading(vc, stripe, dir, key);

  Doc *doc = make_doc(vc, key, c.len, c.hlen);
  run_read_done(vc, c.nbytes);
  CHECK(doc->magic == (c.corrupt ? DOC_CORRUPT : DOC_MAGIC));
}

namespace
{

struct CollisionCase {
  uint32_t len;
  bool     ours;
  bool     corrupt;
};

// The header block is too short to hold an alternate, so a doc that reaches unmarshal_http_info turns corrupt. The fitting
// foreign row proves STORE_COLLISION takes a foreign doc that far, so the unfit foreign row staying intact means it stopped
// before the overrun and was left for the caller's key check.
constexpr std::array<CollisionCase, 3> collision_cases = {
  {
   {4096, false, true},
   {4097, false, false},
   {4097, true, true},
   }
};

} // namespace

TEST_CASE("handleReadDone leaves an unfit Doc that is not ours as a collision", "[cache][read]")
{
  CollisionCase c = GENERATE(from_range(collision_cases));
  INFO("len " << c.len << (c.ours ? " ours" : " not ours"));

  CacheDisk disk;
  init_disk(disk);
  StripeSM           stripe{&disk, 10, 0};
  StripeHeaderFooter header{};
  init_stripe_for_reading(stripe, header);

  Dir      dir = dir_at_stripe_start();
  CacheKey key;
  key.b[0] = 0x1234;
  key.b[1] = 0x5678;
  REQUIRE(stripe.dir_valid(&dir));

  CacheKey other = key;
  other.b[0]     = 0x4321;

  FakeVC vc;
  init_vc_for_reading(vc, stripe, dir, key);

  Doc *doc      = make_doc(vc, c.ours ? key : other, c.len, 8);
  doc->doc_type = CACHE_FRAG_TYPE_HTTP;
  run_read_done(vc, 4096);
  CHECK(doc->magic == (c.corrupt ? DOC_CORRUPT : DOC_MAGIC));
}
