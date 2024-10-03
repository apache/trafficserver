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

#include "P_CacheInternal.h"
#include "StripeSM.h"

#include "tscore/hugepages.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_memory.h"

#include <cstring>

using CacheHTTPInfo = HTTPInfo;

namespace
{

int
compare_ushort(void const *a, void const *b)
{
  return *static_cast<unsigned short const *>(a) - *static_cast<unsigned short const *>(b);
}

} // namespace

struct StripeInitInfo {
  off_t       recover_pos;
  AIOCallback vol_aio[4];
  char       *vol_h_f;

  StripeInitInfo()
  {
    recover_pos = 0;
    vol_h_f     = static_cast<char *>(ats_memalign(ats_pagesize(), 4 * STORE_BLOCK_SIZE));
    memset(vol_h_f, 0, 4 * STORE_BLOCK_SIZE);
  }

  ~StripeInitInfo()
  {
    for (auto &i : vol_aio) {
      i.action = nullptr;
      i.mutex.clear();
    }
    free(vol_h_f);
  }
};

////
// Stripe
//

int
Stripe::dir_check()
{
  static int const SEGMENT_HISTOGRAM_WIDTH           = 16;
  int              hist[SEGMENT_HISTOGRAM_WIDTH + 1] = {0};
  unsigned short   chain_tag[MAX_ENTRIES_PER_SEGMENT];
  int32_t          chain_mark[MAX_ENTRIES_PER_SEGMENT];
  uint64_t         total_buckets = buckets * segments;
  uint64_t         total_entries = total_buckets * DIR_DEPTH;
  int              frag_demographics[1 << DIR_SIZE_WIDTH][DIR_BLOCK_SIZES];

  int j;
  int stale = 0, in_use = 0, empty = 0;
  int free = 0, head = 0, buckets_in_use = 0;

  int     max_chain_length = 0;
  int64_t bytes_in_use     = 0;

  ink_zero(frag_demographics);

  printf("Stripe '[%s]'\n", hash_text.get());
  printf("  Directory Bytes: %" PRIu64 "\n", total_buckets * SIZEOF_DIR);
  printf("  Segments:  %d\n", segments);
  printf("  Buckets per segment:   %" PRIu64 "\n", buckets);
  printf("  Entries:   %" PRIu64 "\n", total_entries);

  for (int s = 0; s < segments; s++) {
    Dir *seg                = this->dir_segment(s);
    int  seg_chain_max      = 0;
    int  seg_empty          = 0;
    int  seg_in_use         = 0;
    int  seg_stale          = 0;
    int  seg_bytes_in_use   = 0;
    int  seg_dups           = 0;
    int  seg_buckets_in_use = 0;

    ink_zero(chain_tag);
    memset(chain_mark, -1, sizeof(chain_mark));

    for (int b = 0; b < buckets; b++) {
      Dir *root = dir_bucket(b, seg);
      int  h    = 0; // chain length starting in this bucket

      // Walk the chain starting in this bucket
      int chain_idx = 0;
      int mark      = 0;
      ++seg_buckets_in_use;
      for (Dir *e = root; e; e = next_dir(e, seg)) {
        if (!dir_offset(e)) {
          ++seg_empty;
          --seg_buckets_in_use;
          // this should only happen on the first dir in a bucket
          ink_assert(nullptr == next_dir(e, seg));
          break;
        } else {
          int e_idx = e - seg;
          ++h;
          chain_tag[chain_idx++] = dir_tag(e);
          if (chain_mark[e_idx] == mark) {
            printf("    - Cycle of length %d detected for bucket %d\n", h, b);
          } else if (chain_mark[e_idx] >= 0) {
            printf("    - Entry %d is in chain %d and %d", e_idx, chain_mark[e_idx], mark);
          } else {
            chain_mark[e_idx] = mark;
          }

          if (!this->dir_valid(e)) {
            ++seg_stale;
          } else {
            uint64_t size = dir_approx_size(e);
            if (dir_head(e)) {
              ++head;
            }
            ++seg_in_use;
            seg_bytes_in_use += size;
            ++frag_demographics[dir_size(e)][dir_big(e)];
          }
        }
      }

      // Check for duplicates (identical tags in the same bucket).
      if (h > 1) {
        unsigned short last;
        qsort(chain_tag, h, sizeof(chain_tag[0]), &compare_ushort);
        last = chain_tag[0];
        for (int k = 1; k < h; ++k) {
          if (last == chain_tag[k]) {
            ++seg_dups;
          }
          last = chain_tag[k];
        }
      }

      ++hist[std::min(h, SEGMENT_HISTOGRAM_WIDTH)];
      seg_chain_max = std::max(seg_chain_max, h);
    }
    int fl_size       = dir_freelist_length(this, s);
    in_use           += seg_in_use;
    empty            += seg_empty;
    stale            += seg_stale;
    free             += fl_size;
    buckets_in_use   += seg_buckets_in_use;
    max_chain_length  = std::max(max_chain_length, seg_chain_max);
    bytes_in_use     += seg_bytes_in_use;

    printf("  - Segment-%d | Entries: used=%d stale=%d free=%d disk-bytes=%d Buckets: used=%d empty=%d max=%d avg=%.2f dups=%d\n",
           s, seg_in_use, seg_stale, fl_size, seg_bytes_in_use, seg_buckets_in_use, seg_empty, seg_chain_max,
           seg_buckets_in_use ? static_cast<float>(seg_in_use + seg_stale) / seg_buckets_in_use : 0.0, seg_dups);
  }

  printf("  - Stripe | Entries: in-use=%d stale=%d free=%d Buckets: empty=%d max=%d avg=%.2f\n", in_use, stale, free, empty,
         max_chain_length, buckets_in_use ? static_cast<float>(in_use + stale) / buckets_in_use : 0);

  printf("    Chain lengths:  ");
  for (j = 0; j < SEGMENT_HISTOGRAM_WIDTH; ++j) {
    printf(" %d=%d ", j, hist[j]);
  }
  printf(" %d>=%d\n", SEGMENT_HISTOGRAM_WIDTH, hist[SEGMENT_HISTOGRAM_WIDTH]);

  char tt[256];
  printf("    Total Size:      %" PRIu64 "\n", static_cast<uint64_t>(len));
  printf("    Bytes in Use:    %" PRIu64 " [%0.2f%%]\n", bytes_in_use, 100.0 * (static_cast<float>(bytes_in_use) / len));
  printf("    Objects:         %d\n", head);
  printf("    Average Size:    %" PRIu64 "\n", head ? (bytes_in_use / head) : 0);
  printf("    Average Frags:   %.2f\n", head ? static_cast<float>(in_use) / head : 0);
  printf("    Write Position:  %" PRIu64 "\n", header->write_pos - start);
  printf("    Wrap Count:      %d\n", header->cycle);
  printf("    Phase:           %s\n", header->phase ? "true" : "false");
  ink_ctime_r(&header->create_time, tt);
  tt[strlen(tt) - 1] = 0;
  printf("    Sync Serial:     %u\n", header->sync_serial);
  printf("    Write Serial:    %u\n", header->write_serial);
  printf("    Create Time:     %s\n", tt);
  printf("\n");
  printf("  Fragment size demographics\n");
  for (int b = 0; b < DIR_BLOCK_SIZES; ++b) {
    int block_size = DIR_BLOCK_SIZE(b);
    int s          = 0;
    while (s < 1 << DIR_SIZE_WIDTH) {
      for (int j = 0; j < 8; ++j, ++s) {
        // The size markings are redundant. Low values (less than DIR_SHIFT_WIDTH) for larger
        // base block sizes should never be used. Such entries should use the next smaller base block size.
        if (b > 0 && s < 1 << DIR_BLOCK_SHIFT(1)) {
          ink_assert(frag_demographics[s][b] == 0);
          continue;
        }
        printf(" %8d[%2d:%1d]:%06d", (s + 1) * block_size, s, b, frag_demographics[s][b]);
      }
      printf("\n");
    }
  }
  printf("\n");

  return 0;
}

void
Stripe::_clear_init()
{
  size_t dir_len = this->dirlen();
  memset(this->raw_dir, 0, dir_len);
  this->_init_dir();
  this->header->magic          = STRIPE_MAGIC;
  this->header->version._major = CACHE_DB_MAJOR_VERSION;
  this->header->version._minor = CACHE_DB_MINOR_VERSION;
  this->scan_pos = this->header->agg_pos = this->header->write_pos = this->start;
  this->header->last_write_pos                                     = this->header->write_pos;
  this->header->phase                                              = 0;
  this->header->cycle                                              = 0;
  this->header->create_time                                        = time(nullptr);
  this->header->dirty                                              = 0;
  this->sector_size = this->header->sector_size = this->disk->hw_sector_size;
  *this->footer                                 = *this->header;
}

void
Stripe::_init_dir()
{
  int b, s, l;

  for (s = 0; s < this->segments; s++) {
    this->header->freelist[s] = 0;
    Dir *seg                  = this->dir_segment(s);
    for (l = 1; l < DIR_DEPTH; l++) {
      for (b = 0; b < this->buckets; b++) {
        Dir *bucket = dir_bucket(b, seg);
        dir_free_entry(dir_bucket_row(bucket, l), s, this);
      }
    }
  }
}

void
Stripe::_init_data_internal()
{
  // step1: calculate the number of entries.
  off_t total_entries = (this->len - (this->start - this->skip)) / cache_config_min_average_object_size;
  // step2: calculate the number of buckets
  off_t total_buckets = total_entries / DIR_DEPTH;
  // step3: calculate the number of segments, no segment has more than 16384 buckets
  this->segments = (total_buckets + (((1 << 16) - 1) / DIR_DEPTH)) / ((1 << 16) / DIR_DEPTH);
  // step4: divide total_buckets into segments on average.
  this->buckets = (total_buckets + this->segments - 1) / this->segments;
  // step5: set the start pointer.
  this->start = this->skip + 2 * this->dirlen();
}

void
Stripe::_init_data(off_t blocks, off_t dir_skip)
{
  len = blocks * STORE_BLOCK_SIZE;
  ink_assert(len <= MAX_STRIPE_SIZE);

  skip = ROUND_TO_STORE_BLOCK((dir_skip < START_POS ? START_POS : dir_skip));

  // successive approximation, directory/meta data eats up some storage
  start = skip;

  // iteratively calculate start + buckets
  this->_init_data_internal();
  this->_init_data_internal();
  this->_init_data_internal();

  data_blocks = (len - (start - skip)) / STORE_BLOCK_SIZE;

  // raw_dir
  raw_dir = nullptr;
  if (ats_hugepage_enabled()) {
    raw_dir = static_cast<char *>(ats_alloc_hugepage(this->dirlen()));
  }
  if (raw_dir == nullptr) {
    raw_dir = static_cast<char *>(ats_memalign(ats_pagesize(), this->dirlen()));
  }

  dir    = reinterpret_cast<Dir *>(raw_dir + this->headerlen());
  header = reinterpret_cast<StripteHeaderFooter *>(raw_dir);
  footer = reinterpret_cast<StripteHeaderFooter *>(raw_dir + this->dirlen() - ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter)));
}

bool
Stripe::flush_aggregate_write_buffer()
{
  // set write limit
  this->header->agg_pos = this->header->write_pos + this->_write_buffer.get_buffer_pos();

  if (!this->_write_buffer.flush(this->fd, this->header->write_pos)) {
    return false;
  }
  this->header->last_write_pos  = this->header->write_pos;
  this->header->write_pos      += this->_write_buffer.get_buffer_pos();
  ink_assert(this->header->write_pos == this->header->agg_pos);
  this->_write_buffer.reset_buffer_pos();
  this->header->write_serial++;

  return true;
}

bool
Stripe::copy_from_aggregate_write_buffer(char *dest, Dir const &dir, size_t nbytes) const
{
  if (!this->dir_agg_buf_valid(&dir)) {
    return false;
  }

  int agg_offset = this->vol_offset(&dir) - this->header->write_pos;
  this->_write_buffer.copy_from(dest, agg_offset, nbytes);
  return true;
}
