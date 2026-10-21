/** @file

  Unit tests for StripeSM::aggWriteDone.

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

#include "../P_CacheDir.h"
#include "../P_CacheInternal.h"

#include <array>
#include <cerrno>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

void
init_writer(FakeVC &vc, StripeSM *stripe)
{
  // Sharing the stripe mutex lets aggWrite(EVENT_CALL) return the writer instead of scheduling a callback to it.
  vc.mutex  = stripe->mutex;
  vc.stripe = stripe;
  rand_CacheKey(&vc.key);
  vc.set_test_data("yay", 4);
  vc.set_write_len(4);
  vc.set_agg_len(stripe->round_to_approx_size(vc.write_len + vc.header_len + vc.frag_len + sizeof(Doc)));
}

// The doc of a head writer carries earliest_key, but openWriteCloseHeadDone registers its entry under first_key.
void
init_head_writer(FakeVC &vc, StripeSM *stripe)
{
  init_writer(vc, stripe);
  vc.f.use_first_key = 1;
  vc.earliest_key    = vc.key;
  rand_CacheKey(&vc.first_key);
}

CacheKey const *
registered_key(FakeVC const &vc)
{
  if (vc.f.use_first_key) {
    return &vc.first_key;
  }
  return &vc.key;
}

} // namespace

class AggWriteErrorTest : public CacheInit
{
public:
  int
  cache_init_success_callback(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */) override
  {
    REQUIRE(CacheProcessor::IsCacheEnabled() == CacheInitState::INITIALIZED);
    REQUIRE(gnstripes >= 1);

    StripeSM *stripe = gstripes[0];
    MUTEX_TRY_LOCK(lock, stripe->mutex, this_ethread());
    if (!lock.is_locked()) {
      CONT_SCHED_LOCK_RETRY(this);
      return EVENT_DONE;
    }

    stripe->clear_dir();

    std::array<FakeVC, 3> writers;
    init_writer(writers[0], stripe);      // case 1 - Sits at write_pos, the start of the buffer.
    init_writer(writers[1], stripe);      // case 2 - Sits past write_pos, so its offset includes its position in the buffer.
    init_head_writer(writers[2], stripe); // case 3 - A head: registered under first_key, not its doc's key.
    int agg_len = 0;
    for (auto &vc : writers) {
      REQUIRE(stripe->add_writer(&vc));
      agg_len += vc.agg_len;
    }

    // Below the high water mark and with no sync writer, aggWrite fills the buffer without starting the disk write.
    stripe->aggWrite(EVENT_CALL, nullptr);
    REQUIRE(!stripe->is_io_in_progress());
    REQUIRE(stripe->get_agg_buf_pos() == agg_len);

    // A writer inserts its entry as soon as its doc is in the buffer, before the disk write completes.
    for (auto &vc : writers) {
      REQUIRE(stripe->directory.insert(registered_key(vc), stripe, &vc.dir));
    }

    int64_t used_before     = ts::Metrics::Gauge::load(cache_rsb.direntries_used);
    int64_t vol_used_before = ts::Metrics::Gauge::load(stripe->cache_vol->vol_rsb.direntries_used);

    stripe->io.aiocb.aio_fildes = stripe->fd;
    stripe->io.aiocb.aio_offset = stripe->directory.header->write_pos;
    stripe->io.aiocb.aio_nbytes = stripe->get_agg_buf_pos();
    stripe->io.aio_result       = -EIO;
    REQUIRE(!stripe->io.ok());
    stripe->aggWriteDone(AIO_EVENT_DONE, nullptr);

    CHECK(stripe->get_agg_buf_pos() == 0);
    CHECK(ts::Metrics::Gauge::load(cache_rsb.direntries_used) == used_before - static_cast<int64_t>(writers.size()));
    CHECK(ts::Metrics::Gauge::load(stripe->cache_vol->vol_rsb.direntries_used) ==
          vol_used_before - static_cast<int64_t>(writers.size()));
    for (auto &vc : writers) {
      CHECK(stripe->directory.remove(registered_key(vc), stripe, &vc.dir) == 0);
    }

    stripe->clear_dir();

    test_done();
    delete this;

    return EVENT_DONE;
  }
};

TEST_CASE("aggWriteDone removes the entries of a failed aggregation write")
{
  init_cache(0);

  AggWriteErrorTest *init = new AggWriteErrorTest;

  this_ethread()->schedule_imm(init);
  this_thread()->execute();
}
