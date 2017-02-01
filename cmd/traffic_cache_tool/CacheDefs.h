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

#if !defined(CACHE_DEFS_H)
#define CACHE_DEFS_H
#include <ts/I_Version.h>
#include <ts/Scalar.h>

namespace tag { struct bytes; }

namespace ApacheTrafficServer
{
constexpr static uint8_t CACHE_DB_MAJOR_VERSION = 24;

typedef Scalar<1, int64_t, tag::bytes> Bytes;
typedef Scalar<1024, int64_t, tag::bytes> Kilobytes;
typedef Scalar<1024 * Kilobytes::SCALE, int64_t, tag::bytes> Megabytes;
typedef Scalar<1024 * Megabytes::SCALE, int64_t, tag::bytes> Gigabytes;
typedef Scalar<1024 * Gigabytes::SCALE, int64_t, tag::bytes> Terabytes;

std::ostream& operator<<(std::ostream& s, Bytes const& n) { return s << n.count() << " bytes"; }
std::ostream& operator<<(std::ostream& s, Kilobytes const& n) { return s << n.count() << " KB"; }
std::ostream& operator<<(std::ostream& s, Megabytes const& n) { return s << n.count() << " MB"; }
std::ostream& operator<<(std::ostream& s, Gigabytes const& n) { return s << n.count() << " HB"; }
std::ostream& operator<<(std::ostream& s, Terabytes const& n) { return s << n.count() << " TB"; }

// Units of allocation for stripes.
 typedef Scalar<128 * Megabytes::SCALE, int64_t, tag::bytes> CacheStripeBlocks;
// Size measurement of cache storage.
// Also size of meta data storage units.
 typedef Scalar<8 * Kilobytes::SCALE, int64_t, tag::bytes> CacheStoreBlocks;
// Size unit for content stored in cache.
 typedef Scalar<512, int64_t, tag::bytes> CacheDataBlocks;

std::ostream& operator<<(std::ostream& s, CacheStripeBlocks const& n) { return s << n.count() << " stripe blocks"; }
std::ostream& operator<<(std::ostream& s, CacheStoreBlocks const& n) { return s << n.count() << " store blocks"; }
std::ostream& operator<<(std::ostream& s, CacheDataBlocks const& n) { return s << n.count() << " data blocks"; }

/** A cache span is a representation of raw storage.
    It corresponds to a raw disk, disk partition, file, or directory.
 */
class CacheSpan
{
public:
  /// Default offset of start of data in a span.
  /// @internal I think this is done to avoid collisions with partition tracking mechanisms.
  static const Bytes OFFSET;
};

/** A section of storage in a span, used to contain a stripe.

    @note Serializable.

    @internal nee @c DiskVolBlock
 */
struct CacheStripeDescriptor {
  Bytes offset;         // offset of start of stripe from start of span.
  CacheStoreBlocks len; // length of block.
  uint32_t vol_idx;     ///< If in use, the volume index.
  unsigned int type : 3;
  unsigned int free : 1;
};

/** Header data for a span.

    @internal nee DiskHeader
 */
struct SpanHeader {
  static constexpr uint32_t MAGIC = 0xABCD1237;
  uint32_t magic;
  uint32_t num_volumes;      /* number of discrete volumes (DiskVol) */
  uint32_t num_free;         /* number of disk volume blocks free */
  uint32_t num_used;         /* number of disk volume blocks in use */
  uint32_t num_diskvol_blks; /* number of disk volume blocks */
  CacheStoreBlocks num_blocks;
  /// Serialized stripe descriptors. This is treated as a variable sized array.
  CacheStripeDescriptor stripes[1];
};

/** Stripe data, serialized format.

    @internal nee VolHeadFooter
 */
class CacheStripeMeta
{
public:
  static constexpr uint32_t MAGIC = 0xF1D0F00D;

  uint32_t magic;
  VersionNumber version;
  time_t create_time;
  off_t write_pos;
  off_t last_write_pos;
  off_t agg_pos;
  uint32_t generation; // token generation (vary), this cannot be 0
  uint32_t phase;
  uint32_t cycle;
  uint32_t sync_serial;
  uint32_t write_serial;
  uint32_t dirty;
  uint32_t sector_size;
  uint32_t unused; // pad out to 8 byte boundary
  uint16_t freelist[1];
};

class StripeData
{
public:
  size_t calc_hdr_len() const;

  int64_t segments; ///< Number of segments.
  int64_t buckets;  ///< Number of buckets.
  off_t skip;       ///< Start of stripe data.
  off_t start;      ///< Start of content data.
  off_t len;        ///< Total size of stripe (metric?)
};

inline size_t
StripeData::calc_hdr_len() const
{
  return sizeof(CacheStripeMeta) + sizeof(uint16_t) * (this->segments - 1);
}
//  inline size_t StripeData::calc_dir_len() const { return this->calc_hdr_len() + this->buckets * DIR_DEPTH * this->segments *
//  SIZEOF_DIR + sizeof(CacheStripeMeta); }

class CacheDirEntry
{
  unsigned int offset : 24;
  unsigned int big : 2;
  unsigned int size : 6;
  unsigned int tag : 12;
  unsigned int phase : 1;
  unsigned int head : 1;
  unsigned int pinnned : 1;
  unsigned int token : 1;
  unsigned int next : 16;
  uint16_t offset_high;
};

class CacheVolume
{
};
}

#endif // CACHE_DEFS_H
