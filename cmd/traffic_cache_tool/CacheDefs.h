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
#include <netinet/in.h>
#include <iostream>
#include <ts/I_Version.h>
#include <ts/INK_MD5.h>
#include <ts/Scalar.h>
#include <ts/Regex.h>
#include <tsconfig/Errata.h>
#include <ts/TextView.h>
#include <ts/ink_file.h>
#include <list>
#include <ts/CryptoHash.h>

#include "Command.h"
#include "File.h"

#if defined(MAGIC)
#undef MAGIC
#endif

namespace tag
{
struct bytes {
  static constexpr char const *const label = " bytes";
};
} // namespace tag

using ts::round_down;
using ts::round_up;

namespace ts
{
#define dir_clear(_e) \
  do {                \
    (_e)->w[0] = 0;   \
    (_e)->w[1] = 0;   \
    (_e)->w[2] = 0;   \
    (_e)->w[3] = 0;   \
    (_e)->w[4] = 0;   \
  } while (0)

#define dir_assign(_e, _x)   \
  do {                       \
    (_e)->w[0] = (_x)->w[0]; \
    (_e)->w[1] = (_x)->w[1]; \
    (_e)->w[2] = (_x)->w[2]; \
    (_e)->w[3] = (_x)->w[3]; \
    (_e)->w[4] = (_x)->w[4]; \
  } while (0)

constexpr static uint8_t CACHE_DB_MAJOR_VERSION = 24;
constexpr static uint8_t CACHE_DB_MINOR_VERSION = 1;
/// Maximum allowed volume index.
constexpr static int MAX_VOLUME_IDX          = 255;
constexpr static int ENTRIES_PER_BUCKET      = 4;
constexpr static int MAX_BUCKETS_PER_SEGMENT = (1 << 16) / ENTRIES_PER_BUCKET;

typedef Scalar<1, off_t, tag::bytes> Bytes;
typedef Scalar<1024, off_t, tag::bytes> Kilobytes;
typedef Scalar<1024 * Kilobytes::SCALE, off_t, tag::bytes> Megabytes;
typedef Scalar<1024 * Megabytes::SCALE, off_t, tag::bytes> Gigabytes;
typedef Scalar<1024 * Gigabytes::SCALE, off_t, tag::bytes> Terabytes;

// Units of allocation for stripes.
typedef Scalar<128 * Megabytes::SCALE, int64_t, tag::bytes> CacheStripeBlocks;
// Size measurement of cache storage.
// Also size of meta data storage units.
typedef Scalar<8 * Kilobytes::SCALE, int64_t, tag::bytes> CacheStoreBlocks;
// Size unit for content stored in cache.
typedef Scalar<512, int64_t, tag::bytes> CacheDataBlocks;

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

    This is stored in the span header to describe the stripes in the span.

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

    This is the serializable descriptor stored in a span.

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
// the counterpart of this structure in ATS is called VolHeaderFooter
class StripeMeta
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

// struct HTTPCacheAlt
struct HTTPCacheAlt {
  HTTPCacheAlt();
  void copy(HTTPCacheAlt *to_copy);
  void copy_frag_offsets_from(HTTPCacheAlt *src);
  void destroy();

  uint32_t m_magic;

  // Writeable is set to true is we reside
  //  in a buffer owned by this structure.
  // INVARIANT: if own the buffer this HttpCacheAlt
  //   we also own the buffers for the request &
  //   response headers
  int32_t m_writeable;
  int32_t m_unmarshal_len;

  int32_t m_id;
  int32_t m_rid;

  int32_t m_object_key[4];
  int32_t m_object_size[2];

  // HTTPHdr m_request_hdr;
  // HTTPHdr m_response_hdr;

  time_t m_request_sent_time;
  time_t m_response_received_time;

  /// # of fragment offsets in this alternate.
  /// @note This is one less than the number of fragments.
  int m_frag_offset_count;
  /// Type of offset for a fragment.
  typedef uint64_t FragOffset;
  /// Table of fragment offsets.
  /// @note The offsets are forward looking so that frag[0] is the
  /// first byte past the end of fragment 0 which is also the first
  /// byte of fragment 1. For this reason there is no fragment offset
  /// for the last fragment.
  FragOffset *m_frag_offsets;
  /// # of fragment offsets built in to object.
  static int constexpr N_INTEGRAL_FRAG_OFFSETS = 4;
  /// Integral fragment offset table.
  FragOffset m_integral_frag_offsets[N_INTEGRAL_FRAG_OFFSETS];

  // With clustering, our alt may be in cluster
  //  incoming channel buffer, when we are
  //  destroyed we decrement the refcount
  //  on that buffer so that it gets destroyed
  // We don't want to use a ref count ptr (Ptr<>)
  //  since our ownership model requires explicit
  //  destroys and ref count pointers defeat this
  // RefCountObj *m_ext_buffer;
};

//
// HTTPCacheAlt::HTTPCacheAlt()
//  : m_magic(CACHE_ALT_MAGIC_ALIVE),
//    m_writeable(1),
//    m_unmarshal_len(-1),
//    m_id(-1),
//    m_rid(-1),
//    m_request_hdr(),
//    m_response_hdr(),
//    m_request_sent_time(0),
//    m_response_received_time(0),
//    m_frag_offset_count(0),
//    m_frag_offsets(nullptr),
//    m_ext_buffer(nullptr)
//{
//  m_object_key[0]  = 0;
//  m_object_key[1]  = 0;
//  m_object_key[2]  = 0;
//  m_object_key[3]  = 0;
//  m_object_size[0] = 0;
//  m_object_size[1] = 0;
//}
//
// void
// HTTPCacheAlt::destroy()
//{
//  ink_assert(m_magic == CACHE_ALT_MAGIC_ALIVE);
//  ink_assert(m_writeable);
//  m_magic     = CACHE_ALT_MAGIC_DEAD;
//  m_writeable = 0;
//  m_request_hdr.destroy();
//  m_response_hdr.destroy();
//  m_frag_offset_count = 0;
//  if (m_frag_offsets && m_frag_offsets != m_integral_frag_offsets) {
//    ats_free(m_frag_offsets);
//    m_frag_offsets = nullptr;
//  }
//  httpCacheAltAllocator.free(this);
//}
//
// void
// HTTPCacheAlt::copy(HTTPCacheAlt *to_copy)
//{
//  m_magic = to_copy->m_magic;
//  // m_writeable =      to_copy->m_writeable;
//  m_unmarshal_len  = to_copy->m_unmarshal_len;
//  m_id             = to_copy->m_id;
//  m_rid            = to_copy->m_rid;
//  m_object_key[0]  = to_copy->m_object_key[0];
//  m_object_key[1]  = to_copy->m_object_key[1];
//  m_object_key[2]  = to_copy->m_object_key[2];
//  m_object_key[3]  = to_copy->m_object_key[3];
//  m_object_size[0] = to_copy->m_object_size[0];
//  m_object_size[1] = to_copy->m_object_size[1];
//
//  if (to_copy->m_request_hdr.valid()) {
//    m_request_hdr.copy(&to_copy->m_request_hdr);
//  }
//
//  if (to_copy->m_response_hdr.valid()) {
//    m_response_hdr.copy(&to_copy->m_response_hdr);
//  }
//
//  m_request_sent_time      = to_copy->m_request_sent_time;
//  m_response_received_time = to_copy->m_response_received_time;
//  this->copy_frag_offsets_from(to_copy);
//}
//
// void
// HTTPCacheAlt::copy_frag_offsets_from(HTTPCacheAlt *src)
//{
//  m_frag_offset_count = src->m_frag_offset_count;
//  if (m_frag_offset_count > 0) {
//    if (m_frag_offset_count > N_INTEGRAL_FRAG_OFFSETS) {
//      /* Mixed feelings about this - technically we don't need it to be a
//         power of two when copied because currently that means it is frozen.
//         But that could change later and it would be a nasty bug to find.
//         So we'll do it for now. The relative overhead is tiny.
//      */
//      int bcount = HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS * 2;
//      while (bcount < m_frag_offset_count) {
//        bcount *= 2;
//      }
//      m_frag_offsets = static_cast<FragOffset *>(ats_malloc(sizeof(FragOffset) * bcount));
//    } else {
//      m_frag_offsets = m_integral_frag_offsets;
//    }
//    memcpy(m_frag_offsets, src->m_frag_offsets, sizeof(FragOffset) * m_frag_offset_count);
//  }
//}

/*
 @internal struct Doc
 */

struct Doc {
  uint32_t magic;        // DOC_MAGIC
  uint32_t len;          // length of this fragment (including hlen & sizeof(Doc), unrounded)
  uint64_t total_len;    // total length of document
  INK_MD5 first_key;     ///< first key in object.
  INK_MD5 key;           ///< Key for this doc.
  uint32_t hlen;         ///< Length of this header.
  uint32_t doc_type : 8; ///< Doc type - indicates the format of this structure and its content.
  uint32_t v_major : 8;  ///< Major version number.
  uint32_t v_minor : 8;  ///< Minor version number.
  uint32_t unused : 8;   ///< Unused, forced to zero.
  uint32_t sync_serial;
  uint32_t write_serial;
  uint32_t pinned; // pinned until
  uint32_t checksum;

  uint32_t data_len();
  uint32_t prefix_len();
  int single_fragment();
  int no_data_in_fragment();
  char *hdr();
  char *data();
};

/*
 @internal struct Dir in P_CacheDir.h
 * size: 10bytes
 */

class CacheDirEntry
{
public:
#if 0
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
#else
  uint16_t w[5];
#endif
};

class CacheVolume
{
};

class URLparser
{
public:
  bool verifyURL(std::string &url1);
  Errata parseURL(TextView URI);
  int getPort(std::string &fullURL, int &port_ptr, int &port_len);

private:
  //   DFA regex;
};

class CacheURL
{
public:
  in_port_t port;
  std::string scheme;
  std::string url;
  std::string hostname;
  std::string path;
  std::string query;
  std::string params;
  std::string fragments;
  std::string user;
  std::string password;
  CacheURL(int port_, ts::TextView b_hostname, ts::TextView b_path, ts::TextView b_params, ts::TextView b_query,
           ts::TextView b_fragments)
  {
    hostname.assign(b_hostname.data(), b_hostname.size());
    port = port_;
    path.assign(b_path.data(), b_path.size());
    params.assign(b_params.data(), b_params.size());
    query.assign(b_query.data(), b_query.size());
    fragments.assign(b_fragments.data(), b_fragments.size());
  }

  CacheURL(ts::TextView blob, int port_)
  {
    url.assign(blob.data(), blob.size());
    port = port_;
  }

  void
  setCredential(char *p_user, int user_len, char *p_pass, int pass_len)
  {
    user.assign(p_user, user_len);
    password.assign(p_pass, pass_len);
  }
};
} // namespace ts

class DFA;
// this class matches url of the format : scheme://hostname:port/path;params?query
struct url_matcher {
  // R"(^https?\:\/\/^[a-z A-Z 0-9]\.[a-z A-Z 0-9 \.]+)"
  url_matcher()
  {
    /*if (regex.compile(R"(^https?\:\/\/^[a-z A-Z 0-9][\. a-z A-Z 0-9 ]+(\:[0-9]\/)?.*))") != 0) {
        std::cout<<"Check your regular expression"<<std::endl;
    }*/
    //  (\w+\:[\w\W]+\@)? (:[0-9]+)?(\/.*)
    if (regex.compile(R"(^(https?\:\/\/)") != 0) {
      std::cout << "Check your regular expression" << std::endl;
      return;
    }
    if (port.compile(R"([0-9]+$)") != 0) {
      std::cout << "Check your regular expression" << std::endl;
      return;
    }
  }

  ~url_matcher() {}
  uint8_t
  match(const char *hostname) const
  {
    if (regex.match(hostname) != -1) {
      return 1;
    }
    //   if(url_with_user.match(hostname) != -1)
    //       return 2;
    return 0;
  }
  uint8_t
  portmatch(const char *hostname, int length) const
  {
    if (port.match(hostname, length) != -1) {
      return 1;
    }
    //   if(url_with_user.match(hostname) != -1)
    //       return 2;
    return 0;
  }

private:
  DFA port;
  DFA regex;
};

using ts::Bytes;
using ts::Megabytes;
using ts::CacheStoreBlocks;
using ts::CacheStripeBlocks;
using ts::StripeMeta;
using ts::CacheStripeDescriptor;
using ts::Errata;
using ts::FilePath;
using ts::CacheDirEntry;
using ts::MemSpan;
using ts::Doc;

constexpr int ESTIMATED_OBJECT_SIZE     = 8000;
constexpr int DEFAULT_HW_SECTOR_SIZE    = 512;
constexpr int VOL_HASH_TABLE_SIZE       = 32707;
constexpr unsigned short VOL_HASH_EMPTY = 65535;
constexpr int DIR_TAG_WIDTH             = 12;
constexpr int DIR_DEPTH                 = 4;
constexpr int SIZEOF_DIR                = 10;
constexpr int MAX_ENTRIES_PER_SEGMENT   = (1 << 16);
constexpr int DIR_SIZE_WIDTH            = 6;
constexpr int DIR_BLOCK_SIZES           = 4;
constexpr int CACHE_BLOCK_SHIFT         = 9;
constexpr int CACHE_BLOCK_SIZE          = (1 << CACHE_BLOCK_SHIFT); // 512, smallest sector size

namespace ct
{
struct Stripe;
struct Span {
  Span(FilePath const &path) : _path(path) {}
  Errata load();
  Errata loadDevice();
  bool isEmpty() const;
  int header_len = 0;

  /// Replace all existing stripes with a single unallocated stripe covering the span.
  Errata clear();

  /// This is broken and needs to be cleaned up.
  void clearPermanently();

  ts::Rv<Stripe *> allocStripe(int vol_idx, CacheStripeBlocks len);
  Errata updateHeader(); ///< Update serialized header and write to disk.

  FilePath _path;           ///< File system location of span.
  ats_scoped_fd _fd;        ///< Open file descriptor for span.
  int _vol_idx = 0;         ///< Forced volume.
  CacheStoreBlocks _base;   ///< Offset to first usable byte.
  CacheStoreBlocks _offset; ///< Offset to first content byte.
  // The space between _base and _offset is where the span information is stored.
  CacheStoreBlocks _len;         ///< Total length of span.
  CacheStoreBlocks _free_space;  ///< Total size of free stripes.
  ink_device_geometry _geometry; ///< Geometry of span.
  uint64_t num_usable_blocks;    // number of usable blocks for stripes i.e., after subtracting the skip and the disk header.
  /// Local copy of serialized header data stored on in the span.
  std::unique_ptr<ts::SpanHeader> _header;
  /// Live information about stripes.
  /// Seeded from @a _header and potentially agumented with direct probing.
  std::list<Stripe *> _stripes;
};
/* --------------------------------------------------------------------------------------- */
struct Stripe {
  /// Meta data is stored in 4 copies A/B and Header/Footer.
  enum Copy { A = 0, B = 1 };
  enum { HEAD = 0, FOOT = 1 };

  /// Piece wise memory storage for the directory.
  struct Chunk {
    Bytes _start; ///< Starting offset relative to physical device of span.
    Bytes _skip;  ///< # of bytes not valid at the start of the first block.
    Bytes _clip;  ///< # of bytes not valid at the end of the last block.

    typedef std::vector<MemSpan> Chain;
    Chain _chain; ///< Chain of blocks.

    ~Chunk();

    void append(MemSpan m);
    void clear();
  };

  /// Construct from span header data.
  Stripe(Span *span, Bytes start, CacheStoreBlocks len);

  /// Is stripe unallocated?
  bool isFree() const;

  /** Probe a chunk of memory @a mem for stripe metadata.

      @a mem is updated to remove memory that has been probed. If @a
      meta is not @c nullptr then it is used for additional cross
      checking.

      @return @c true if @a mem has valid data, @c false otherwise.
  */
  bool probeMeta(MemSpan &mem, StripeMeta const *meta = nullptr);

  /// Check a buffer for being valid stripe metadata.
  /// @return @c true if valid, @c false otherwise.
  static bool validateMeta(StripeMeta const *meta);

  /// Load metadata for this stripe.
  Errata loadMeta();
  Errata loadDir();
  int check_loop(int s);
  void dir_check();
  bool walk_bucket_chain(int s); // returns true if there is a loop
  void walk_all_buckets();

  /// Initialize the live data from the loaded serialized data.
  void updateLiveData(enum Copy c);

  Span *_span;           ///< Hosting span.
  CryptoHash hash_id;    /// hash_id
  Bytes _start;          ///< Offset of first byte of stripe metadata.
  Bytes _content;        ///< Start of content.
  CacheStoreBlocks _len; ///< Length of stripe.
  uint8_t _vol_idx = 0;  ///< Volume index.
  uint8_t _type    = 0;  ///< Stripe type.
  int8_t _idx      = -1; ///< Stripe index in span.
  int agg_buf_pos  = 0;

  int64_t _buckets;  ///< Number of buckets per segment.
  int64_t _segments; ///< Number of segments.

  std::string hashText;

  /// Meta copies, indexed by A/B then HEAD/FOOT.
  StripeMeta _meta[2][2];
  /// Locations for the meta data.
  CacheStoreBlocks _meta_pos[2][2];
  /// Directory.
  Chunk _directory;
  CacheDirEntry const *dir = nullptr; // the big buffer that will hold the whole directory of stripe header.
  uint16_t *freelist       = nullptr; // using this freelist instead of the one in StripeMeta.
                                      // This is because the freelist is not being copied to _metap[2][2] correctly.
  // need to do something about it .. hmmm :-?
  int dir_freelist_length(int s);
  TS_INLINE CacheDirEntry *dir_segment(int s);
  TS_INLINE CacheDirEntry *vol_dir_segment(int s);
  int64_t stripe_offset(CacheDirEntry *e); // offset of e w.r.t the stripe
  size_t vol_dirlen();
  TS_INLINE int vol_headerlen();
  void vol_init_data_internal();
  void vol_init_data();
  void dir_init_segment(int s);
  void dir_free_entry(CacheDirEntry *e, int s);
  CacheDirEntry *dir_delete_entry(CacheDirEntry *e, CacheDirEntry *p, int s);
  //  int dir_bucket_length(CacheDirEntry *b, int s);
  int dir_probe(CryptoHash *key, CacheDirEntry *result, CacheDirEntry **last_collision);
  bool dir_valid(CacheDirEntry *e);
  bool validate_sync_serial();
  Errata updateHeaderFooter();
  Errata InitializeMeta();
  void init_dir();
  Errata clear(); // clears striped headers and footers
};
} // namespace ct
