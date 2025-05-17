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

#pragma once

#include "AggregateWriteBuffer.h"
#include "P_CacheDir.h"
#include "P_CacheDisk.h"
#include "P_CacheStats.h"

#include "iocore/cache/Store.h"

#include "tscore/ink_align.h"
#include "tscore/ink_memory.h"

#include <cstddef>
#include <cstdint>

#define CACHE_BLOCK_SHIFT        9
#define CACHE_BLOCK_SIZE         (1 << CACHE_BLOCK_SHIFT) // 512, smallest sector size
#define ROUND_TO_STORE_BLOCK(_x) INK_ALIGN((_x), STORE_BLOCK_SIZE)
#define ROUND_TO_CACHE_BLOCK(_x) INK_ALIGN((_x), CACHE_BLOCK_SIZE)
#define ROUND_TO_SECTOR(_p, _x)  INK_ALIGN((_x), _p->sector_size)
#define ROUND_TO(_x, _y)         INK_ALIGN((_x), (_y))

#define STRIPE_BLOCK_SIZE (1024 * 1024 * 128) // 128MB
#define MIN_STRIPE_SIZE   STRIPE_BLOCK_SIZE
#define MAX_STRIPE_SIZE   ((off_t)512 * 1024 * 1024 * 1024 * 1024) // 512TB

// This is defined here so CacheVC can avoid including StripeSM.h.
#define RECOVERY_SIZE EVACUATION_SIZE // 8MB

struct CacheVol {
  int          vol_number       = -1;
  CacheType    scheme           = CacheType::NONE;
  off_t        size             = 0;
  int          num_vols         = 0;
  int          avg_obj_size     = -1; // Defer to the records.config if not overriden
  int          fragment_size    = -1; // Defer to the records.config if not overriden
  bool         ramcache_enabled = true;
  StripeSM   **stripes          = nullptr;
  DiskStripe **disk_stripes     = nullptr;
  LINK(CacheVol, link);
  // per volume stats
  CacheStatsBlock vol_rsb;

  CacheVol() {}
};

class Stripe
{
public:
  ats_scoped_str hash_text;
  int            frag_size{-1};

  Directory directory;
  off_t     scan_pos{};
  off_t     skip{};  // start of headers
  off_t     start{}; // start of data
  off_t     len{};

  uint32_t sector_size{};

  CacheVol *cache_vol{};

  /**
   * Stripe constructor.
   *
   * @param disk: The disk object to associate with this stripe.
   * The disk path must be non-null.
   * @param blocks: Number of blocks. Must be at least 10.
   * @param dir_skip: Offset into the disk at which to start the stripe.
   * If this value is less than START_POS, START_POS will be used instead.
   *
   * @see START_POS
   */
  Stripe(CacheDisk *disk, off_t blocks, off_t dir_skip, int avg_obj_size = -1, int fragment_size = -1);

  int dir_check();

  uint32_t round_to_approx_size(uint32_t l) const;

  // inline functions
  /* Calculates the total length of the vol header and the freelist.
   */
  int headerlen() const;
  /* Calculates the total length of the header, directories and footer.
   */
  size_t dirlen() const;

  bool dir_valid(const Dir *dir) const;
  bool dir_agg_valid(const Dir *dir) const;
  bool dir_agg_buf_valid(const Dir *dir) const;

  int vol_out_of_phase_valid(Dir const *e) const;
  int vol_out_of_phase_agg_valid(Dir const *e) const;
  int vol_out_of_phase_write_valid(Dir const *e) const;
  int vol_in_phase_valid(Dir const *e) const;
  int vol_in_phase_agg_buf_valid(Dir const *e) const;

  off_t vol_offset(Dir const *e) const;
  off_t offset_to_vol_offset(off_t pos) const;
  off_t vol_offset_to_offset(off_t pos) const;
  /* Length of the partition not including the offset of location 0.
   */
  off_t vol_relative_length(off_t start_offset) const;

  int get_agg_buf_pos() const;

  /**
   * Retrieve a document from the aggregate write buffer.
   *
   * This is used to speed up reads by copying from the in-memory write buffer
   * instead of reading from disk. If the document is not in the write buffer,
   * nothing will be copied.
   *
   * @param dir: The directory entry for the desired document.
   * @param dest: The destination buffer where the document will be copied to.
   * @param nbytes: The size of the document (number of bytes to copy).
   * @return Returns true if the document was copied, false otherwise.
   */
  bool copy_from_aggregate_write_buffer(char *dest, Dir const &dir, size_t nbytes) const;

protected:
  off_t                data_blocks{};
  AggregateWriteBuffer _write_buffer;

  void _clear_init(std::uint32_t hw_sector_size);
  void _init_dir();
  bool flush_aggregate_write_buffer(int fd);

private:
  void _init_hash_text(CacheDisk const *disk, off_t blocks, off_t dir_skip);
  void _init_data(off_t store_block_size, int avg_obj_size = -1);
  void _init_data_internal(int avg_obj_size = -1); // Defaults to cache_config_min_average_object_size;
  void _init_directory(std::size_t directory_size, int header_size, int footer_size);
};

inline uint32_t
Stripe::round_to_approx_size(uint32_t l) const
{
  uint32_t ll = round_to_approx_dir_size(l);
  return ROUND_TO_SECTOR(this, ll);
}

inline int
Stripe::headerlen() const
{
  return ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter) + sizeof(uint16_t) * (this->directory.segments - 1));
}

inline size_t
Stripe::dirlen() const
{
  return this->headerlen() +
         ROUND_TO_STORE_BLOCK(((size_t)this->directory.buckets) * DIR_DEPTH * this->directory.segments * SIZEOF_DIR) +
         ROUND_TO_STORE_BLOCK(sizeof(StripteHeaderFooter));
}

/**
  entry is valid
 */
inline bool
Stripe::dir_valid(const Dir *dir) const
{
  return (this->directory.header->phase == dir_phase(dir) ? this->vol_in_phase_valid(dir) : this->vol_out_of_phase_valid(dir));
}

/**
  entry is valid and outside of write aggregation region
 */
inline bool
Stripe::dir_agg_valid(const Dir *dir) const
{
  return (this->directory.header->phase == dir_phase(dir) ? this->vol_in_phase_valid(dir) : this->vol_out_of_phase_agg_valid(dir));
}

inline bool
Stripe::dir_agg_buf_valid(const Dir *dir) const
{
  return (this->directory.header->phase == dir_phase(dir) && this->vol_in_phase_agg_buf_valid(dir));
}

inline int
Stripe::vol_out_of_phase_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 >= ((this->directory.header->agg_pos - this->start) / CACHE_BLOCK_SIZE));
}

inline int
Stripe::vol_out_of_phase_agg_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 >= ((this->directory.header->agg_pos - this->start + AGG_SIZE) / CACHE_BLOCK_SIZE));
}

inline int
Stripe::vol_out_of_phase_write_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 >= ((this->directory.header->write_pos - this->start) / CACHE_BLOCK_SIZE));
}

inline int
Stripe::vol_in_phase_valid(Dir const *e) const
{
  return (dir_offset(e) - 1 <
          ((this->directory.header->write_pos + this->_write_buffer.get_buffer_pos() - this->start) / CACHE_BLOCK_SIZE));
}

inline int
Stripe::vol_in_phase_agg_buf_valid(Dir const *e) const
{
  return (this->vol_offset(e) >= this->directory.header->write_pos &&
          this->vol_offset(e) < (this->directory.header->write_pos + this->_write_buffer.get_buffer_pos()));
}

inline off_t
Stripe::vol_offset(Dir const *e) const
{
  return this->start + static_cast<off_t>(dir_offset(e)) * CACHE_BLOCK_SIZE - CACHE_BLOCK_SIZE;
}

inline off_t
Stripe::offset_to_vol_offset(off_t pos) const
{
  return ((pos - this->start + CACHE_BLOCK_SIZE) / CACHE_BLOCK_SIZE);
}

inline off_t
Stripe::vol_offset_to_offset(off_t pos) const
{
  return this->start + pos * CACHE_BLOCK_SIZE - CACHE_BLOCK_SIZE;
}

inline off_t
Stripe::vol_relative_length(off_t start_offset) const
{
  return (this->len + this->skip) - start_offset;
}

inline int
Stripe::get_agg_buf_pos() const
{
  return this->_write_buffer.get_buffer_pos();
}
