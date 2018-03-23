/** @file

    Main program file for Cache Tool.

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

#include <iostream>
#include <list>
#include <memory>
#include <vector>
#include <map>
#include <getopt.h>
#include <system_error>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <time.h>
#include <bitset>
#include <inttypes.h>

#include <ts/ink_memory.h>
#include <ts/ink_file.h>
#include <ts/BufferWriter.h>
#include <ts/CryptoHash.h>

#include "File.h"
#include "CacheDefs.h"
#include "Command.h"

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

// constexpr int STORE_BLOCK_SIZE           = 8192;
// constexpr int STORE_BLOCK_SHIFT          = 13;
constexpr int ESTIMATED_OBJECT_SIZE  = 8000;
constexpr int DEFAULT_HW_SECTOR_SIZE = 512;
// constexpr int MIN_VOL_SIZE               = 1024 * 1024 * 128;
constexpr int VOL_HASH_TABLE_SIZE        = 32707;
int cache_config_min_average_object_size = ESTIMATED_OBJECT_SIZE;
CacheStoreBlocks Vol_hash_alloc_size(1024);
constexpr unsigned short VOL_HASH_EMPTY = 65535;
constexpr int DIR_TAG_WIDTH             = 12;
constexpr int DIR_DEPTH                 = 4;
constexpr int SIZEOF_DIR                = 10;
constexpr int MAX_ENTRIES_PER_SEGMENT   = (1 << 16);
constexpr int DIR_SIZE_WIDTH            = 6;
constexpr int DIR_BLOCK_SIZES           = 4;
constexpr int CACHE_BLOCK_SHIFT         = 9;
constexpr int CACHE_BLOCK_SIZE          = (1 << CACHE_BLOCK_SHIFT); // 512, smallest sector size
const Bytes ts::CacheSpan::OFFSET{CacheStoreBlocks{1}};

enum { SILENT = 0, NORMAL, VERBOSE } Verbosity = NORMAL;

namespace
{
FilePath SpanFile;
FilePath VolumeFile;

ts::CommandTable Commands;

// Default this to read only, only enable write if specifically required.
int OPEN_RW_FLAG = O_RDONLY;

struct Stripe;

struct Span {
  Span(FilePath const &path) : _path(path) {}
  Errata load();
  Errata loadDevice();
  //  Errata Initialize();
  //  Errata create_stripe(int number, off_t size_in_blocks, int scheme);
  /// No allocated stripes on this span.
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
  INK_MD5 hash_id;       /// hash_id
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
  int dir_probe(INK_MD5 *key, CacheDirEntry *result, CacheDirEntry **last_collision);
  bool dir_valid(CacheDirEntry *e);
  bool validate_sync_serial();
  Errata updateHeaderFooter();
  Errata InitializeMeta();
  void init_dir();
  Errata clear(); // clears striped headers and footers
};

bool
Stripe::validate_sync_serial()
{
  // check if A sync_serials match and A is at least as updated as B
  return (_meta[0][0].sync_serial == _meta[0][1].sync_serial &&
          (_meta[0][0].sync_serial >= _meta[1][0].sync_serial ||
           _meta[1][0].sync_serial != _meta[1][1].sync_serial)) || // OR check if B's sync_serials match
         (_meta[1][0].sync_serial == _meta[1][1].sync_serial);
}

Errata
Stripe::clear()
{
  Errata zret;
  alignas(512) static char zero[CacheStoreBlocks::SCALE]; // should be all zero, it's static.
  pwrite(_span->_fd, zero, sizeof(zero), this->_meta_pos[A][HEAD]);
  pwrite(_span->_fd, zero, sizeof(zero), this->_meta_pos[A][FOOT]);
  pwrite(_span->_fd, zero, sizeof(zero), this->_meta_pos[B][HEAD]);
  pwrite(_span->_fd, zero, sizeof(zero), this->_meta_pos[B][FOOT]);
  return zret;
}
Stripe::Chunk::~Chunk()
{
  this->clear();
}
void
Stripe::Chunk::append(MemSpan m)
{
  _chain.push_back(m);
}
void
Stripe::Chunk::clear()
{
  for (auto &m : _chain)
    free(const_cast<void *>(m.data()));
  _chain.clear();
}

Stripe::Stripe(Span *span, Bytes start, CacheStoreBlocks len) : _span(span), _start(start), _len(len)
{
  ts::bwprint(hashText, "{} {}:{}", span->_path.path(), _start, _len.count());
  printf("hash id of stripe is hash of %.*s\n", static_cast<int>(hashText.size()), hashText.data());
}

bool
Stripe::isFree() const
{
  return 0 == _vol_idx;
}

// TODO: Implement the whole logic
Errata
Stripe::InitializeMeta()
{
  Errata zret;
  // memset(this->raw_dir, 0, dir_len);
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      _meta[i][j].magic             = StripeMeta::MAGIC;
      _meta[i][j].version.ink_major = ts::CACHE_DB_MAJOR_VERSION;
      _meta[i][j].version.ink_minor = ts::CACHE_DB_MINOR_VERSION;
      _meta[i][j].agg_pos = _meta[i][j].last_write_pos = _meta[i][j].write_pos = this->_start;
      _meta[i][j].phase = _meta[i][j].cycle = _meta[i][j].sync_serial = _meta[i][j].write_serial = _meta[i][j].dirty = 0;
      _meta[i][j].create_time = time(nullptr);
      _meta[i][j].sector_size = DEFAULT_HW_SECTOR_SIZE;
    }
  }
  if (!freelist) // freelist is not allocated yet
  {
    freelist = (uint16_t *)malloc(_segments * sizeof(uint16_t)); // segments has already been calculated
  }
  if (!dir) // for new spans, this will likely be nullptr as we don't need to read the stripe meta from disk
  {
    char *raw_dir = (char *)ats_memalign(ats_pagesize(), this->vol_dirlen());
    dir           = (CacheDirEntry *)(raw_dir + this->vol_headerlen());
  }
  init_dir();
  return zret;
}

// Need to be bit more robust at some point.
bool
Stripe::validateMeta(StripeMeta const *meta)
{
  // Need to be bit more robust at some point.
  return StripeMeta::MAGIC == meta->magic && meta->version.ink_major <= ts::CACHE_DB_MAJOR_VERSION &&
         meta->version.ink_minor <= 2 // This may have always been zero, actually.
    ;
}

bool
Stripe::probeMeta(MemSpan &mem, StripeMeta const *base_meta)
{
  while (mem.usize() >= sizeof(StripeMeta)) {
    StripeMeta const *meta = mem.ptr<StripeMeta>(0);
    if (this->validateMeta(meta) && (base_meta == nullptr ||               // no base version to check against.
                                     (meta->version == base_meta->version) // need more checks here I think.
                                     )) {
      return true;
    }
    // The meta data is stored aligned on a stripe block boundary, so only need to check there.
    mem += CacheStoreBlocks::SCALE;
  }
  return false;
}

/* INK_ALIGN() is only to be used to align on a power of 2 boundary */
#define INK_ALIGN(size, boundary) (((size) + ((boundary)-1)) & ~((boundary)-1))

#define ROUND_TO_STORE_BLOCK(_x) INK_ALIGN((_x), 8192)

Errata
Stripe::updateHeaderFooter()
{
  Errata zret;
  this->vol_init_data();

  int64_t hdr_size    = this->vol_headerlen();
  int64_t dir_size    = this->vol_dirlen();
  Bytes footer_offset = Bytes(dir_size - ROUND_TO_STORE_BLOCK(sizeof(StripeMeta)));
  _meta_pos[A][HEAD]  = round_down(_start);
  _meta_pos[A][FOOT]  = round_down(_start + footer_offset);
  _meta_pos[B][HEAD]  = round_down(this->_start + Bytes(dir_size));
  _meta_pos[B][FOOT]  = round_down(this->_start + Bytes(dir_size) + footer_offset);
  std::cout << "updating header " << _meta_pos[0][0] << std::endl;
  std::cout << "updating header " << _meta_pos[0][1] << std::endl;
  std::cout << "updating header " << _meta_pos[1][0] << std::endl;
  std::cout << "updating header " << _meta_pos[1][1] << std::endl;
  InitializeMeta();

  if (!OPEN_RW_FLAG) {
    zret.push(0, 1, "Writing Not Enabled.. Please use --write to enable writing to disk");
    return zret;
  }

  char *meta_t = (char *)ats_memalign(ats_pagesize(), dir_size);
  // copy headers
  for (auto i : {A, B}) {
    // copy header
    memcpy(meta_t, &_meta[i][HEAD], sizeof(StripeMeta));
    // copy freelist
    memcpy(meta_t + sizeof(StripeMeta) - sizeof(uint16_t), this->freelist, this->_segments * sizeof(uint16_t));

    ssize_t n = pwrite(_span->_fd, meta_t, hdr_size, _meta_pos[i][HEAD]);
    if (n < hdr_size) {
      std::cout << "problem writing header to disk: " << strerror(errno) << ":"
                << " " << n << "<" << hdr_size << std::endl;
      zret = Errata::Message(0, errno, "Failed to write stripe header ");
      return zret;
    }
    // copy dir entries
    dir_size = dir_size - hdr_size - ROUND_TO_STORE_BLOCK(sizeof(StripeMeta));
    memcpy(meta_t, (char *)dir, dir_size);
    n = pwrite(_span->_fd, meta_t, dir_size, _meta_pos[i][HEAD] + hdr_size); //
    if (n < dir_size) {
      std::cout << "problem writing dir to disk: " << strerror(errno) << ":"
                << " " << n << "<" << dir_size << std::endl;
      zret = Errata::Message(0, errno, "Failed to write stripe header ");
      return zret;
    }

    // copy footer,
    memcpy(meta_t, &_meta[i][FOOT], sizeof(StripeMeta));

    int64_t footer_size = ROUND_TO_STORE_BLOCK(sizeof(StripeMeta));
    n                   = pwrite(_span->_fd, meta_t, footer_size, _meta_pos[i][FOOT]);
    if (n < footer_size) {
      std::cout << "problem writing footer to disk: " << strerror(errno) << ":"
                << " " << n << "<" << footer_size << std::endl;
      zret = Errata::Message(0, errno, "Failed to write stripe header ");
      return zret;
    }
  }
  ats_memalign_free(meta_t);
  return zret;
}

TS_INLINE int
Stripe::vol_headerlen()
{
  return ROUND_TO_STORE_BLOCK(sizeof(StripeMeta) + sizeof(uint16_t) * (this->_segments - 1));
}

size_t
Stripe::vol_dirlen()
{
  return vol_headerlen() + ROUND_TO_STORE_BLOCK(((size_t)this->_buckets) * DIR_DEPTH * this->_segments * SIZEOF_DIR) +
         ROUND_TO_STORE_BLOCK(sizeof(StripeMeta));
}

void
Stripe::vol_init_data_internal()
{
  this->_buckets =
    ((this->_len.count() * 8192 - (this->_content - this->_start)) / cache_config_min_average_object_size) / DIR_DEPTH;
  this->_segments = (this->_buckets + (((1 << 16) - 1) / DIR_DEPTH)) / ((1 << 16) / DIR_DEPTH);
  this->_buckets  = (this->_buckets + this->_segments - 1) / this->_segments;
  this->_content  = this->_start + Bytes(2 * vol_dirlen());
}

void
Stripe::vol_init_data()
{
  // iteratively calculate start + buckets
  this->vol_init_data_internal();
  this->vol_init_data_internal();
  this->vol_init_data_internal();
}

void
Stripe::updateLiveData(enum Copy c)
{
  //  CacheStoreBlocks delta{_meta_pos[c][FOOT] - _meta_pos[c][HEAD]};
  CacheStoreBlocks header_len(0);
  //  int64_t n_buckets;
  //  int64_t n_segments;

  _content = _start;
  /*
   * COMMENTING THIS SECTION FOR NOW TO USE THE EXACT LOGIN USED IN ATS TO CALCULATE THE NUMBER OF SEGMENTS AND BUCKETS
  // Past the header is the segment free list heads which if sufficiently long (> ~4K) can take
  // more than 1 store block. Start with a guess of 1 and adjust upwards as needed. A 2TB stripe
  // with an AOS of 8000 has roughly 3700 segments meaning that for even 10TB drives this loop
  // should only be a few iterations.
  do {
    ++header_len;
    n_buckets  = Bytes(delta - header_len) / (sizeof(CacheDirEntry) * ts::ENTRIES_PER_BUCKET);
    n_segments = n_buckets / ts::MAX_BUCKETS_PER_SEGMENT;
    // This should never be more than one loop, usually none.
    while ((n_buckets / n_segments) > ts::MAX_BUCKETS_PER_SEGMENT)
      ++n_segments;
  } while ((sizeof(StripeMeta) + sizeof(uint16_t) * n_segments) > static_cast<size_t>(header_len));

  _buckets         = n_buckets / n_segments;
  _segments        = n_segments;
  */
  _directory._skip = header_len;
}

#define dir_big(_e) ((uint32_t)((((_e)->w[1]) >> 8) & 0x3))
#define dir_bit(_e, _w, _b) ((uint32_t)(((_e)->w[_w] >> (_b)) & 1))
#define dir_size(_e) ((uint32_t)(((_e)->w[1]) >> 10))
#define dir_approx_size(_e) ((dir_size(_e) + 1) * DIR_BLOCK_SIZE(dir_big(_e)))
#define dir_head(_e) dir_bit(_e, 2, 13)
#define DIR_MASK_TAG(_t) ((_t) & ((1 << DIR_TAG_WIDTH) - 1))
#define dir_tag(_e) ((uint32_t)((_e)->w[2] & ((1 << DIR_TAG_WIDTH) - 1)))
#define dir_offset(_e) \
  ((int64_t)(((uint64_t)(_e)->w[0]) | (((uint64_t)((_e)->w[1] & 0xFF)) << 16) | (((uint64_t)(_e)->w[4]) << 24)))
#define dir_set_offset(_e, _o)                                              \
  do {                                                                      \
    (_e)->w[0] = (uint16_t)_o;                                              \
    (_e)->w[1] = (uint16_t)((((_o) >> 16) & 0xFF) | ((_e)->w[1] & 0xFF00)); \
    (_e)->w[4] = (uint16_t)((_o) >> 24);                                    \
  } while (0)
//#define dir_segment(_s, _d) vol_dir_segment(_d, _s)
#define dir_in_seg(_s, _i) ((CacheDirEntry *)(((char *)(_s)) + (SIZEOF_DIR * (_i))))
#define dir_next(_e) (_e)->w[3]
#define dir_phase(_e) dir_bit(_e, 2, 12)
#define DIR_BLOCK_SHIFT(_i) (3 * (_i))
#define DIR_BLOCK_SIZE(_i) (CACHE_BLOCK_SIZE << DIR_BLOCK_SHIFT(_i))
#define dir_set_prev(_e, _o) (_e)->w[2] = (uint16_t)(_o)
#define dir_set_next(_e, _o) (_e)->w[3] = (uint16_t)(_o)

bool
dir_compare_tag(const CacheDirEntry *e, const INK_MD5 *key)
{
  return (dir_tag(e) == DIR_MASK_TAG(key->slice32(2)));
}

TS_INLINE CacheDirEntry *
Stripe::dir_segment(int s)
{
  return vol_dir_segment(s);
}

TS_INLINE CacheDirEntry *
Stripe::vol_dir_segment(int s)
{
  return (CacheDirEntry *)(((char *)this->dir) + (s * this->_buckets) * DIR_DEPTH * SIZEOF_DIR);
}

TS_INLINE CacheDirEntry *
dir_bucket(int64_t b, CacheDirEntry *seg)
{
  return dir_in_seg(seg, b * DIR_DEPTH);
}

TS_INLINE CacheDirEntry *
dir_from_offset(int64_t i, CacheDirEntry *seg)
{
#if DIR_DEPTH < 5
  if (!i)
    return 0;
  return dir_in_seg(seg, i);
#else
  i = i + ((i - 1) / (DIR_DEPTH - 1));
  return dir_in_seg(seg, i);
#endif
}

TS_INLINE int
vol_in_phase_valid(Stripe *d, CacheDirEntry *e)
{
  return (dir_offset(e) - 1 < ((d->_meta[0][0].write_pos + d->agg_buf_pos - d->_start) / CACHE_BLOCK_SIZE));
}

TS_INLINE int
vol_out_of_phase_valid(Stripe *d, CacheDirEntry *e)
{
  return (dir_offset(e) - 1 >= ((d->_meta[0][0].agg_pos - d->_start) / CACHE_BLOCK_SIZE));
}

TS_INLINE CacheDirEntry *
next_dir(CacheDirEntry *d, CacheDirEntry *seg)
{
  int i = dir_next(d);
  return dir_from_offset(i, seg);
}
#define dir_offset(_e) \
  ((int64_t)(((uint64_t)(_e)->w[0]) | (((uint64_t)((_e)->w[1] & 0xFF)) << 16) | (((uint64_t)(_e)->w[4]) << 24)))

TS_INLINE CacheDirEntry *
dir_bucket_row(CacheDirEntry *b, int64_t i)
{
  return dir_in_seg(b, i);
}

TS_INLINE int64_t
dir_to_offset(const CacheDirEntry *d, const CacheDirEntry *seg)
{
#if DIR_DEPTH < 5
  return (((char *)d) - ((char *)seg)) / SIZEOF_DIR;
#else
  int64_t i = (int64_t)((((char *)d) - ((char *)seg)) / SIZEOF_DIR);
  i         = i - (i / DIR_DEPTH);
  return i;
#endif
}

bool
Stripe::dir_valid(CacheDirEntry *_e)
{
  return (this->_meta[0][0].phase == dir_phase(_e) ? vol_in_phase_valid(this, _e) : vol_out_of_phase_valid(this, _e));
}

int64_t
Stripe::stripe_offset(CacheDirEntry *e)
{
  return this->_content + (int64_t)(dir_offset(e) * CACHE_BLOCK_SIZE) - CACHE_BLOCK_SIZE;
}

int
Stripe::dir_probe(INK_MD5 *key, CacheDirEntry *result, CacheDirEntry **last_collision)
{
  int segment = key->slice32(0) % this->_segments;
  int bucket  = key->slice32(1) % this->_buckets;

  CacheDirEntry *seg = this->dir_segment(segment);
  CacheDirEntry *e   = nullptr;
  e                  = dir_bucket(bucket, seg);
  char *stripe_buff2 = (char *)malloc(dir_approx_size(e));
  Doc *doc           = nullptr;
  // TODO: collision craft is pending.. look at the main ATS code. Assuming no collision for now
  if (dir_offset(e)) {
    do {
      if (dir_compare_tag(e, key)) {
        if (dir_valid(e)) {
          std::cout << "dir_probe hit: found seg: " << segment << " bucket: " << bucket << " offset: " << dir_offset(e)
                    << std::endl;
          break;
        } else {
          // let's skip deleting for now
          // e = dir_delete_entry(e, p ,segment);
          // continue;
        }
      }
      e = next_dir(e, seg);

    } while (e);
    int fd         = _span->_fd;
    int64_t offset = stripe_offset(e);
    int64_t size   = dir_approx_size(e);
    pread(fd, stripe_buff2, size, offset);

    doc = reinterpret_cast<Doc *>(stripe_buff2);
    std::string hdr(doc->hdr(), doc->hlen);
    std::cout << "HEADER\n" << hdr << std::endl;

    std::string data_(doc->data(), doc->data_len());
    std::cout << "DATA\n" << data_ << std::endl;
  } else {
    std::cout << "Not found in the Cache" << std::endl;
  }
  free(stripe_buff2);
  return 0; // Why does this have a non-void return?
}

CacheDirEntry *
Stripe::dir_delete_entry(CacheDirEntry *e, CacheDirEntry *p, int s)
{
  CacheDirEntry *seg      = dir_segment(s);
  int no                  = dir_next(e);
  this->_meta[0][0].dirty = 1;
  if (p) {
    unsigned int fo = this->freelist[s];
    unsigned int eo = dir_to_offset(e, seg);
    dir_clear(e);
    dir_set_next(p, no);
    dir_set_next(e, fo);
    if (fo) {
      dir_set_prev(dir_from_offset(fo, seg), eo);
    }
    this->freelist[s] = eo;
  } else {
    CacheDirEntry *n = next_dir(e, seg);
    if (n) {
      dir_assign(e, n);
      dir_delete_entry(n, e, s);
      return e;
    } else {
      dir_clear(e);
      return nullptr;
    }
  }
  return dir_from_offset(no, seg);
}

void
Stripe::walk_all_buckets()
{
  for (int s = 0; s < this->_segments; s++) {
    if (walk_bucket_chain(s))
      std::cout << "Loop present in Segment " << s << std::endl;
  }
}

bool
Stripe::walk_bucket_chain(int s)
{
  CacheDirEntry *seg = this->dir_segment(s);
  std::bitset<65536> b_bitset;
  b_bitset.reset();
  for (int b = 0; b < this->_buckets; b++) {
    CacheDirEntry *p = nullptr;
    auto *dir_b      = dir_bucket(b, seg);
    CacheDirEntry *e = dir_b;
    int len          = 0;

    while (e) {
      len++;
      int i = dir_to_offset(e, seg);
      if (b_bitset.test(i)) {
        std::cout << "bit already set in "
                  << "seg " << s << " bucket " << b << std::endl;
      }
      if (i > 0) // i.e., not the first dir in the segment
        b_bitset[i] = 1;

#if 1
      if (!dir_valid(e) || !dir_offset(e)) {
        // std::cout<<"dir_clean in segment "<<s<<" =>cleaning "<<e<<" tag"<<dir_tag(e)<<" boffset"<< dir_offset(e)<< " bucket:
        // "<<dir_b<< " bucket len: "<<dir_bucket_length(dir_b, s)<<std::endl;
        e = dir_delete_entry(e, p, s);
        continue;
      }
#endif
      p = e;
      e = next_dir(e, seg);
    }
    //    std::cout<<"dir len in this bucket "<<len<<std::endl;
  }
  return false;
}

void
Stripe::dir_free_entry(CacheDirEntry *e, int s)
{
  CacheDirEntry *seg = this->dir_segment(s);
  unsigned int fo    = this->freelist[s];
  unsigned int eo    = dir_to_offset(e, seg);
  dir_set_next(e, fo);
  if (fo) {
    dir_set_prev(dir_from_offset(fo, seg), eo);
  }
  this->freelist[s] = eo;
}

// adds all the directory entries
// in a segment to the segment freelist
void
Stripe::dir_init_segment(int s)
{
  this->freelist[s]  = 0;
  CacheDirEntry *seg = this->dir_segment(s);
  int l, b;
  memset(seg, 0, SIZEOF_DIR * DIR_DEPTH * this->_buckets);
  for (l = 1; l < DIR_DEPTH; l++) {
    for (b = 0; b < this->_buckets; b++) {
      CacheDirEntry *bucket = dir_bucket(b, seg);
      this->dir_free_entry(dir_bucket_row(bucket, l), s);
    }
  }
}

void
Stripe::init_dir()
{
  for (int s = 0; s < this->_segments; s++) {
    this->freelist[s]  = 0;
    CacheDirEntry *seg = this->dir_segment(s);
    int l, b;
    for (l = 1; l < DIR_DEPTH; l++) {
      for (b = 0; b < this->_buckets; b++) {
        CacheDirEntry *bucket = dir_bucket(b, seg);
        this->dir_free_entry(dir_bucket_row(bucket, l), s);
        // std::cout<<"freelist"<<this->freelist[s]<<std::endl;
      }
    }
  }
}

Errata
Stripe::loadDir()
{
  Errata zret;
  char *raw_dir = (char *)ats_memalign(ats_pagesize(), this->vol_dirlen());
  dir           = (CacheDirEntry *)(raw_dir + this->vol_headerlen());
  // read directory
  pread(this->_span->_fd, raw_dir, this->vol_dirlen(), this->_start);
  return zret;
}
//
// Cache Directory
//

#if 0
// return value 1 means no loop
// zero indicates loop
int
dir_bucket_loop_check(CacheDirEntry *start_dir, CacheDirEntry *seg)
{
  if (start_dir == nullptr) {
    return 1;
  }

  CacheDirEntry *p1 = start_dir;
  CacheDirEntry *p2 = start_dir;

  while (p2) {
    // p1 moves by one entry per iteration
    assert(p1);
    p1 = next_dir(p1, seg);
    // p2 moves by two entries per iteration
    p2 = next_dir(p2, seg);
    if (p2) {
      p2 = next_dir(p2, seg);
    } else {
      return 1;
    }

    if (p2 == p1) {
      return 0; // we have a loop
    }
  }
  return 1;
}
#endif

int
Stripe::dir_freelist_length(int s)
{
  int free           = 0;
  CacheDirEntry *seg = this->dir_segment(s);
  CacheDirEntry *e   = dir_from_offset(this->freelist[s], seg);
  if (this->check_loop(s)) {
    return (DIR_DEPTH - 1) * this->_buckets;
  }
  while (e) {
    free++;
    e = next_dir(e, seg);
  }
  return free;
}

int
Stripe::check_loop(int s)
{
  // look for loop in the segment
  // rewrite the freelist if loop is present
  CacheDirEntry *seg = this->dir_segment(s);
  CacheDirEntry *e   = dir_from_offset(this->freelist[s], seg);
  std::bitset<65536> f_bitset;
  f_bitset.reset();
  while (e) {
    int i = dir_next(e);
    if (f_bitset.test(i)) {
      // bit was set in a previous round so a loop is present
      std::cout << "<check_loop> Loop present in Span" << this->_span->_path.path() << " Stripe: " << this->hashText
                << "Segment: " << s << std::endl;
      this->dir_init_segment(s);
      return 1;
    }
    f_bitset[i] = 1;
    e           = dir_from_offset(i, seg);
  }

  return 0;
}

int
compare_ushort(void const *a, void const *b)
{
  return *static_cast<unsigned short const *>(a) - *static_cast<unsigned short const *>(b);
}

void
Stripe::dir_check()
{
  static int const SEGMENT_HISTOGRAM_WIDTH = 16;
  int hist[SEGMENT_HISTOGRAM_WIDTH + 1]    = {0};
  unsigned short chain_tag[MAX_ENTRIES_PER_SEGMENT];
  int32_t chain_mark[MAX_ENTRIES_PER_SEGMENT];

  this->loadMeta();
  this->loadDir();
  //  uint64_t total_buckets = _segments * _buckets;
  //  uint64_t total_entries = total_buckets * DIR_DEPTH;
  int frag_demographics[1 << DIR_SIZE_WIDTH][DIR_BLOCK_SIZES];
  int j;
  int stale = 0, in_use = 0, empty = 0;
  int free = 0, head = 0, buckets_in_use = 0;

  int max_chain_length = 0;
  int64_t bytes_in_use = 0;
  std::cout << "Stripe '[" << hashText << "]'" << std::endl;
  std::cout << "  Directory Bytes: " << _segments * _buckets * SIZEOF_DIR << std::endl;
  std::cout << "  Segments:  " << _segments << std::endl;
  std::cout << "  Buckets per segment:  " << _buckets << std::endl;
  std::cout << "  Entries:  " << _segments * _buckets * DIR_DEPTH << std::endl;
  for (int s = 0; s < _segments; s++) {
    CacheDirEntry *seg     = this->dir_segment(s);
    int seg_chain_max      = 0;
    int seg_empty          = 0;
    int seg_in_use         = 0;
    int seg_stale          = 0;
    int seg_bytes_in_use   = 0;
    int seg_dups           = 0;
    int seg_buckets_in_use = 0;

    ink_zero(chain_tag);
    memset(chain_mark, -1, sizeof(chain_mark));
    for (int b = 0; b < _buckets; b++) {
      CacheDirEntry *root = dir_bucket(b, seg);
      int h               = 0;
      int chain_idx       = 0;
      int mark            = 0;
      ++seg_buckets_in_use;
      // walking through the directories
      for (CacheDirEntry *e = root; e; e = next_dir(e, seg)) {
        if (!dir_offset(e)) {
          ++seg_empty;
          --seg_buckets_in_use;
          // this should only happen on the first dir in a bucket
          assert(nullptr == next_dir(e, seg));
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

          if (!dir_valid(e)) {
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
        e = next_dir(e, seg);
        if (!e) {
          break;
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
    int fl_size = dir_freelist_length(s);
    in_use += seg_in_use;
    empty += seg_empty;
    stale += seg_stale;
    free += fl_size;
    buckets_in_use += seg_buckets_in_use;
    max_chain_length = std::max(max_chain_length, seg_chain_max);
    bytes_in_use += seg_bytes_in_use;

    printf("  - Segment-%d | Entries: used=%d stale=%d free=%d disk-bytes=%d Buckets: used=%d empty=%d max=%d avg=%.2f dups=%d\n",
           s, seg_in_use, seg_stale, fl_size, seg_bytes_in_use, seg_buckets_in_use, seg_empty, seg_chain_max,
           seg_buckets_in_use ? static_cast<float>(seg_in_use + seg_stale) / seg_buckets_in_use : 0.0, seg_dups);
  }
  //////////////////

  printf("  - Stripe | Entries: in-use=%d stale=%d free=%d Buckets: empty=%d max=%d avg=%.2f\n", in_use, stale, free, empty,
         max_chain_length, buckets_in_use ? static_cast<float>(in_use + stale) / buckets_in_use : 0);

  printf("    Chain lengths:  ");
  for (j = 0; j < SEGMENT_HISTOGRAM_WIDTH; ++j) {
    printf(" %d=%d ", j, hist[j]);
  }
  printf(" %d>=%d\n", SEGMENT_HISTOGRAM_WIDTH, hist[SEGMENT_HISTOGRAM_WIDTH]);

  char tt[256];
  printf("    Total Size:      %" PRIu64 "\n", static_cast<uint64_t>(_len.count()));
  printf("    Bytes in Use:    %" PRIu64 " [%0.2f%%]\n", bytes_in_use, 100.0 * (static_cast<float>(bytes_in_use) / _len.count()));
  printf("    Objects:         %d\n", head);
  printf("    Average Size:    %" PRIu64 "\n", head ? (bytes_in_use / head) : 0);
  printf("    Average Frags:   %.2f\n", head ? static_cast<float>(in_use) / head : 0);
  printf("    Write Position:  %" PRIu64 "\n", _meta[0][0].write_pos - _content.count());
  printf("    Wrap Count:      %d\n", _meta[0][0].cycle);
  printf("    Phase:           %s\n", _meta[0][0].phase ? "true" : "false");
  ctime_r(&_meta[0][0].create_time, tt);
  tt[strlen(tt) - 1] = 0;
  printf("    Sync Serial:     %u\n", _meta[0][0].sync_serial);
  printf("    Write Serial:    %u\n", _meta[0][0].write_serial);
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
          assert(frag_demographics[s][b] == 0);
          continue;
        }
        printf(" %8d[%2d:%1d]:%06d", (s + 1) * block_size, s, b, frag_demographics[s][b]);
      }
      printf("\n");
    }
  }
  printf("\n");
  ////////////////
}

Errata
Stripe::loadMeta()
{
  // Read from disk in chunks of this size. This needs to be a multiple of both the
  // store block size and the directory entry size so neither goes acrss read boundaries.
  // Beyond that the value should be in the ~10MB range for what I guess is best performance
  // vs. blocking production disk I/O on a live system.
  constexpr static int64_t N = (1 << 8) * CacheStoreBlocks::SCALE * sizeof(CacheDirEntry);

  Errata zret;

  int fd = _span->_fd;
  Bytes n;
  bool found;
  MemSpan data; // The current view of the read buffer.
  Bytes delta;
  Bytes pos = _start;
  // Avoid searching the entire span, because some of it must be content. Assume that AOS is more than 160
  // which means at most 10/160 (1/16) of the span can be directory/header.
  Bytes limit     = pos + _len / 16;
  size_t io_align = _span->_geometry.blocksz;
  StripeMeta const *meta;

  std::unique_ptr<char> bulk_buff;                      // Buffer for bulk reads.
  static const size_t SBSIZE = CacheStoreBlocks::SCALE; // save some typing.
  alignas(SBSIZE) char stripe_buff[SBSIZE];             // Use when reading a single stripe block.
  alignas(SBSIZE) char stripe_buff2[SBSIZE];            // use to save the stripe freelist
  if (io_align > SBSIZE)
    return Errata::Message(0, 1, "Cannot load stripe ", _idx, " on span ", _span->_path, " because the I/O block alignment ",
                           io_align, " is larger than the buffer alignment ", SBSIZE);

  _directory._start = pos;
  // Header A must be at the start of the stripe block.
  // Todo: really need to check pread() for failure.
  ssize_t headerbyteCount = pread(fd, stripe_buff2, SBSIZE, pos);
  n.assign(headerbyteCount);
  data.assign(stripe_buff2, n);
  meta = data.ptr<StripeMeta>(0);
  // TODO:: We need to read more data at this point  to populate dir
  if (this->validateMeta(meta)) {
    delta              = Bytes(data.ptr<char>(0) - stripe_buff2);
    _meta[A][HEAD]     = *meta;
    _meta_pos[A][HEAD] = round_down(pos + Bytes(delta));
    pos += round_up(SBSIZE);
    _directory._skip = Bytes(SBSIZE); // first guess, updated in @c updateLiveData when the header length is computed.
    // Search for Footer A. Nothing for it except to grub through the disk.
    // The searched data is cached so it's available for directory parsing later if needed.
    while (pos < limit) {
      char *buff = static_cast<char *>(ats_memalign(io_align, N));
      bulk_buff.reset(buff);
      n.assign(pread(fd, buff, N, pos));
      data.assign(buff, n);
      found = this->probeMeta(data, &_meta[A][HEAD]);
      if (found) {
        ptrdiff_t diff     = data.ptr<char>(0) - buff;
        _meta[A][FOOT]     = data.template at<StripeMeta>(0);
        _meta_pos[A][FOOT] = round_down(pos + Bytes(diff));
        // don't bother attaching block if the footer is at the start
        if (diff > 0) {
          _directory._clip = Bytes(N - diff);
          _directory.append({bulk_buff.release(), N});
        }
        data += SBSIZE; // skip footer for checking on B copy.
        break;
      } else {
        _directory.append({bulk_buff.release(), N});
        pos += round_up(N);
      }
    }
  } else {
    zret.push(0, 1, "Header A not found");
  }
  pos = _meta_pos[A][FOOT];
  // Technically if Copy A is valid, Copy B is not needed. But at this point it's cheap to retrieve
  // (as the exact offset is computable).
  if (_meta_pos[A][FOOT] > 0) {
    delta = _meta_pos[A][FOOT] - _meta_pos[A][HEAD];
    // Header B should be immediately after Footer A. If at the end of the last read,
    // do another read.
    //    if (data.size() < CacheStoreBlocks::SCALE) {
    //      pos += round_up(N);
    //      n = Bytes(pread(fd, stripe_buff, CacheStoreBlocks::SCALE, pos));
    //      data.assign(stripe_buff, n);
    //    }
    pos  = this->_start + Bytes(vol_dirlen());
    meta = data.ptr<StripeMeta>(0);
    if (this->validateMeta(meta)) {
      _meta[B][HEAD]     = *meta;
      _meta_pos[B][HEAD] = round_down(pos);

      // Footer B must be at the same relative offset to Header B as Footer A -> Header A.
      pos += delta;
      n = Bytes(pread(fd, stripe_buff, ts::CacheStoreBlocks::SCALE, pos));
      data.assign(stripe_buff, n);
      meta = data.ptr<StripeMeta>(0);
      if (this->validateMeta(meta)) {
        _meta[B][FOOT]     = *meta;
        _meta_pos[B][FOOT] = round_down(pos);
      }
    }
  }

  if (_meta_pos[A][FOOT] > 0) {
    if (_meta[A][HEAD].sync_serial == _meta[A][FOOT].sync_serial &&
        (0 == _meta_pos[B][FOOT] || _meta[B][HEAD].sync_serial != _meta[B][FOOT].sync_serial ||
         _meta[A][HEAD].sync_serial >= _meta[B][HEAD].sync_serial)) {
      this->updateLiveData(A);
    } else if (_meta_pos[B][FOOT] > 0 && _meta[B][HEAD].sync_serial == _meta[B][FOOT].sync_serial) {
      this->updateLiveData(B);
    } else {
      zret.push(0, 1, "Invalid stripe data - candidates found but sync serial data not valid. ", _meta[A][HEAD].sync_serial, ":",
                _meta[A][FOOT].sync_serial, ":", _meta[B][HEAD].sync_serial, ":", _meta[B][FOOT].sync_serial);
    }
  }

  n.assign(headerbyteCount);
  data.assign(stripe_buff2, n);
  meta = data.ptr<StripeMeta>(0);
  // copy freelist
  freelist = (uint16_t *)malloc(_segments * sizeof(uint16_t));
  for (int i    = 0; i < _segments; i++)
    freelist[i] = meta->freelist[i];

  if (!zret)
    _directory.clear();
  return zret;
}

/* --------------------------------------------------------------------------------------- */
/// A live volume.
/// Volume data based on data from loaded spans.
struct Volume {
  int _idx;               ///< Volume index.
  CacheStoreBlocks _size; ///< Amount of storage allocated.
  std::vector<Stripe *> _stripes;

  /// Remove all data related to @a span.
  //  void clearSpan(Span *span);
  /// Remove all allocated space and stripes.
  void clear();
};

#if 0
void
Volume::clearSpan(Span* span)
{
  auto spot = std::remove_if(_stripes.begin(), _stripes.end(), [span,this](Stripe* stripe) { return stripe->_span == span ? ( this->_size -= stripe->_len , true ) : false; });
  _stripes.erase(spot, _stripes.end());
}
#endif

void
Volume::clear()
{
  _size.assign(0);
  _stripes.clear();
}
/* --------------------------------------------------------------------------------------- */
/// Data parsed from the volume config file.
struct VolumeConfig {
  Errata load(FilePath const &path);

  /// Data direct from the config file.
  struct Data {
    int _idx     = 0;         ///< Volume index.
    int _percent = 0;         ///< Size if specified as a percent.
    Megabytes _size{0};       ///< Size if specified as an absolute.
    CacheStripeBlocks _alloc; ///< Allocation size.

    // Methods handy for parsing
    bool
    hasSize() const
    {
      return _percent > 0 || _size > 0;
    }
    bool
    hasIndex() const
    {
      return _idx > 0;
    }
  };

  std::vector<Data> _volumes;
  typedef std::vector<Data>::iterator iterator;
  typedef std::vector<Data>::const_iterator const_iterator;

  iterator
  begin()
  {
    return _volumes.begin();
  }
  iterator
  end()
  {
    return _volumes.end();
  }
#if 0
  const_iterator
  begin() const
  {
    return _volumes.begin();
  }
  const_iterator
  end() const
  {
    return _volumes.end();
  }
#endif
  //  Errata validatePercentAllocation();
  void convertToAbsolute(ts::CacheStripeBlocks total_span_size);
};

#if 0
Errata
VolumeConfig::validatePercentAllocation()
{
  Errata zret;
  int n = 0;
  for (auto &vol : _volumes)
    n += vol._percent;
  if (n > 100)
    zret.push(0, 10, "Volume percent allocation ", n, " is more than 100%");
  return zret;
}
#endif

void
VolumeConfig::convertToAbsolute(ts::CacheStripeBlocks n)
{
  for (auto &vol : _volumes) {
    if (vol._percent) {
      vol._alloc.assign((n.count() * vol._percent + 99) / 100);
    } else {
      vol._alloc = round_up(vol._size);
    }
  }
}
/* --------------------------------------------------------------------------------------- */
struct Cache {
  ~Cache();

  Errata loadSpan(FilePath const &path);
  Errata loadSpanConfig(FilePath const &path);
  Errata loadSpanDirect(FilePath const &path, int vol_idx = -1, Bytes size = Bytes(-1));
  Errata loadURLs(FilePath const &path);

  Errata allocStripe(Span *span, int vol_idx, CacheStripeBlocks len);

  /// Change the @a span to have a single, unused stripe occupying the entire @a span.
  //  void clearSpan(Span *span);
  /// Clear all allocated space.
  void clearAllocation();

  enum class SpanDumpDepth { SPAN, STRIPE, DIRECTORY };
  void dumpSpans(SpanDumpDepth depth);
  void dumpVolumes();
  void build_stripe_hash_table();
  Stripe *key_to_stripe(INK_MD5 *key, const char *hostname, int host_len);
  //  ts::CacheStripeBlocks calcTotalSpanPhysicalSize();
  ts::CacheStripeBlocks calcTotalSpanConfiguredSize();

  std::list<Span *> _spans;
  std::map<int, Volume> _volumes;
  std::vector<Stripe *> globalVec_stripe;
  std::unordered_set<ts::CacheURL *> URLset;
  unsigned short *stripes_hash_table;
};

Errata
Cache::allocStripe(Span *span, int vol_idx, CacheStripeBlocks len)
{
  auto rv = span->allocStripe(vol_idx, len);
  std::cout << span->_path << ":" << vol_idx << std::endl;
  if (rv.isOK()) {
    _volumes[vol_idx]._stripes.push_back(rv);
  }
  return rv.errata();
}

#if 0
void
Cache::clearSpan(Span* span)
{
  for ( auto& item : _volumes ) item.second.clearSpan(span);
  span->clear();
}
#endif

void
Cache::clearAllocation()
{
  for (auto span : _spans)
    span->clear();
  for (auto &item : _volumes)
    item.second.clear();
}
/* --------------------------------------------------------------------------------------- */
/// Temporary structure used for doing allocation computations.
class VolumeAllocator
{
  /// Working struct that tracks allocation information.
  struct V {
    VolumeConfig::Data const &_config; ///< Configuration instance.
    CacheStripeBlocks _size;           ///< Current actual size.
    int64_t _deficit;                  ///< fractional deficit
    int64_t _shares;                   ///< relative amount of free space to allocate

    V(VolumeConfig::Data const &config, CacheStripeBlocks size, int64_t deficit = 0, int64_t shares = 0)
      : _config(config), _size(size), _deficit(deficit), _shares(shares)
    {
    }
    V &
    operator=(V const &that)
    {
      new (this) V(that._config, that._size, that._deficit, that._shares);
      return *this;
    }
  };

  typedef std::vector<V> AV;
  AV _av; ///< Working vector of volume data.

  Cache _cache;       ///< Current state.
  VolumeConfig _vols; ///< Configuration state.

public:
  VolumeAllocator();

  Errata load(FilePath const &spanFile, FilePath const &volumeFile);
  Errata fillEmptySpans();
  Errata fillAllSpans();
  Errata allocateSpan(FilePath const &spanFile);
  void dumpVolumes();

protected:
  /// Update the allocation for a span.
  Errata allocateFor(Span &span);
};

VolumeAllocator::VolumeAllocator()
{
}

Errata
VolumeAllocator::load(FilePath const &spanFile, FilePath const &volumeFile)
{
  Errata zret;

  if (!volumeFile)
    zret.push(0, 9, "Volume config file not set");
  if (!spanFile)
    zret.push(0, 9, "Span file not set");

  if (zret) {
    zret = _vols.load(volumeFile);
    if (zret) {
      zret = _cache.loadSpan(spanFile);
      if (zret) {
        CacheStripeBlocks total = _cache.calcTotalSpanConfiguredSize();
        _vols.convertToAbsolute(total);
        for (auto &vol : _vols) {
          CacheStripeBlocks size(0);
          auto spot = _cache._volumes.find(vol._idx);
          if (spot != _cache._volumes.end())
            size = round_down(spot->second._size);
          _av.push_back({vol, size, 0, 0});
        }
      }
    }
  }
  return zret;
}

void
VolumeAllocator::dumpVolumes()
{
  _cache.dumpVolumes();
}

Errata
VolumeAllocator::fillEmptySpans()
{
  Errata zret;
  // Walk the spans, skipping ones that are not empty.
  for (auto span : _cache._spans) {
    if (span->isEmpty())
      this->allocateFor(*span);
  }
  return zret;
}

Errata
VolumeAllocator::allocateSpan(FilePath const &input_file_path)
{
  Errata zret;
  for (auto span : _cache._spans) {
    if (0 == strcmp(span->_path.path(), input_file_path.path())) {
      std::cout << "===============================" << std::endl;
      if (span->_header) {
        zret.push(0, 1, "Disk already initialized with valid header");
      } else {
        this->allocateFor(*span);
        span->updateHeader();
        for (auto &strp : span->_stripes) {
          strp->updateHeaderFooter();
        }
      }
    }
  }
  for (auto &_v : _av) {
    std::cout << _v._size << std::endl;
  }
  return zret;
}

Errata
VolumeAllocator::fillAllSpans()
{
  Errata zret;
  // clear all current volume allocations.
  for (auto &v : _av) {
    v._size.assign(0);
  }
  // Allocate for each span, clearing as it goes.
  _cache.clearAllocation();
  for (auto span : _cache._spans) {
    this->allocateFor(*span);
  }
  return zret;
}

Errata
VolumeAllocator::allocateFor(Span &span)
{
  Errata zret;

  /// Scaling factor for shares, effectively the accuracy.
  static const int64_t SCALE = 1000;
  int64_t total_shares       = 0;

  if (Verbosity >= NORMAL)
    std::cout << "Allocating " << CacheStripeBlocks(round_down(span._len)).count() << " stripe blocks from span " << span._path
              << std::endl;

  // Walk the volumes and get the relative allocations.
  for (auto &v : _av) {
    auto delta = v._config._alloc - v._size;
    if (delta > 0) {
      v._deficit = (delta.count() * SCALE) / v._config._alloc.count();
      v._shares  = delta.count() * v._deficit;
      total_shares += v._shares;
    } else {
      v._shares = 0;
    }
  }
  assert(total_shares != 0);
  // Now allocate blocks.
  CacheStripeBlocks span_blocks(round_down(span._free_space));
  CacheStripeBlocks span_used{0};

  // sort by deficit so least relatively full volumes go first.
  std::sort(_av.begin(), _av.end(), [](V const &lhs, V const &rhs) { return lhs._deficit > rhs._deficit; });
  for (auto &v : _av) {
    if (v._shares) {
      CacheStripeBlocks n{(((span_blocks - span_used).count() * v._shares) + total_shares - 1) / total_shares};
      CacheStripeBlocks delta{v._config._alloc - v._size};
      // Not sure why this is needed. But a large and empty volume can dominate the shares
      // enough to get more than it actually needs if the other volume are relative small or full.
      // I need to do more math to see if the weighting can be adjusted to not have this happen.
      n = std::min(n, delta);
      v._size += n;
      span_used += n;
      total_shares -= v._shares;
      Errata z = _cache.allocStripe(&span, v._config._idx, round_up(n));
      if (Verbosity >= NORMAL)
        std::cout << "           " << n << " to volume " << v._config._idx << std::endl;
      if (!z)
        std::cout << z;
    }
  }
  if (Verbosity >= NORMAL)
    std::cout << "     Total " << span_used << std::endl;
  if (OPEN_RW_FLAG) {
    if (Verbosity >= NORMAL)
      std::cout << " Updating Header ... ";
    zret = span.updateHeader();
  }
  _cache.dumpVolumes(); // debug
  if (Verbosity >= NORMAL) {
    if (zret)
      std::cout << " Done" << std::endl;
    else
      std::cout << " Error" << std::endl << zret;
  }

  return zret;
}
/* --------------------------------------------------------------------------------------- */
Errata
Cache::loadSpan(FilePath const &path)
{
  Errata zret;
  if (!path.has_path())
    zret = Errata::Message(0, EINVAL, "A span file specified by --span is required");
  else if (!path.is_readable())
    zret = Errata::Message(0, EPERM, '\'', path.path(), "' is not readable.");
  else if (path.is_regular_file())
    zret = this->loadSpanConfig(path);
  else
    zret = this->loadSpanDirect(path);
  return zret;
}

Errata
Cache::loadSpanDirect(FilePath const &path, int vol_idx, Bytes size)
{
  Errata zret;
  std::unique_ptr<Span> span(new Span(path));
  zret = span->load();
  if (zret) {
    if (span->_header) {
      int nspb = span->_header->num_diskvol_blks;
      for (auto i = 0; i < nspb; ++i) {
        ts::CacheStripeDescriptor &raw = span->_header->stripes[i];
        Stripe *stripe                 = new Stripe(span.get(), raw.offset, raw.len);
        stripe->_idx                   = i;
        if (raw.free == 0) {
          stripe->_vol_idx = raw.vol_idx;
          stripe->_type    = raw.type;
          _volumes[stripe->_vol_idx]._stripes.push_back(stripe);
          _volumes[stripe->_vol_idx]._size += stripe->_len;
          stripe->vol_init_data();
        } else {
          span->_free_space += stripe->_len;
        }
        span->_stripes.push_back(stripe);
        globalVec_stripe.push_back(stripe);
      }
      span->_vol_idx = vol_idx;
    } else {
      span->clear();
    }
    _spans.push_back(span.release());
  }
  return zret;
}

Errata
Cache::loadSpanConfig(FilePath const &path)
{
  static const ts::TextView TAG_ID("id");
  static const ts::TextView TAG_VOL("volume");

  Errata zret;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::TextView content = cfile.content();
    while (content) {
      ts::TextView line = content.take_prefix_at('\n');
      line.ltrim_if(&isspace);
      if (line.empty() || '#' == *line)
        continue;
      ts::TextView path = line.take_prefix_if(&isspace);
      if (path) {
        // After this the line is [size] [id=string] [volume=#]
        while (line) {
          ts::TextView value(line.take_prefix_if(&isspace));
          if (value) {
            ts::TextView tag(value.take_prefix_at('='));
            if (!tag) { // must be the size
            } else if (0 == strcasecmp(tag, TAG_ID)) {
            } else if (0 == strcasecmp(tag, TAG_VOL)) {
              ts::TextView text;
              auto n = ts::svtoi(value, &text);
              if (text == value && 0 < n && n < 256) {
              } else {
                zret.push(0, 0, "Invalid volume index '", value, "'");
              }
            }
          }
        }
        zret = this->loadSpan(FilePath(path));
      }
    }
  } else {
    zret = Errata::Message(0, EBADF, "Unable to load ", path);
  }
  return zret;
}

Errata
Cache::loadURLs(FilePath const &path)
{
  static const ts::TextView TAG_VOL("url");
  ts::URLparser parser;
  Errata zret;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::TextView content = cfile.content();

    while (!content.empty()) {
      ts::TextView blob = content.take_prefix_at('\n');

      ts::TextView tag(blob.take_prefix_at('='));
      if (tag.empty()) {
      } else if (0 == strcasecmp(tag, TAG_VOL)) {
        std::string url;
        url.assign(blob.data(), blob.size());
        int port_ptr = -1, port_len = -1;
        int port = parser.getPort(url, port_ptr, port_len);
        if (port_ptr >= 0 && port_len > 0)
          url.erase(port_ptr, port_len + 1); // get rid of :PORT
        std::cout << "port # " << port << ":" << port_ptr << ":" << port_len << ":" << url << std::endl;
        ts::CacheURL *curl = new ts::CacheURL(url, port);
        URLset.emplace(curl);
      }
    }
  } else {
    zret = Errata::Message(0, EBADF, "Unable to load ", path);
  }
  return zret;
}

void
Cache::dumpSpans(SpanDumpDepth depth)
{
  if (depth >= SpanDumpDepth::SPAN) {
    for (auto span : _spans) {
      if (nullptr == span->_header) {
        std::cout << "Span: " << span->_path << " is uninitialized" << std::endl;
      } else {
        std::cout << "\n----------------------------------\n"
                  << "Span: " << span->_path << "\n----------------------------------\n"
                  << "#Magic: " << span->_header->magic << " #Volumes: " << span->_header->num_volumes
                  << "  #in use: " << span->_header->num_used << "  #free: " << span->_header->num_free
                  << "  #stripes: " << span->_header->num_diskvol_blks << "  Len(bytes): " << span->_header->num_blocks.value()
                  << std::endl;

        for (auto stripe : span->_stripes) {
          std::cout << "\n>>>>>>>>> Stripe " << static_cast<int>(stripe->_idx) << " @ " << stripe->_start
                    << " len=" << stripe->_len.count() << " blocks "
                    << " vol=" << static_cast<int>(stripe->_vol_idx) << " type=" << static_cast<int>(stripe->_type) << " "
                    << (stripe->isFree() ? "free" : "in-use") << std::endl;

          std::cout << "      " << stripe->_segments << " segments with " << stripe->_buckets << " buckets per segment for "
                    << stripe->_buckets * stripe->_segments * ts::ENTRIES_PER_BUCKET << " total directory entries taking "
                    << stripe->_buckets * stripe->_segments * sizeof(CacheDirEntry) * ts::ENTRIES_PER_BUCKET
                    //                        << " out of " << (delta-header_len).units() << " bytes."
                    << std::endl;
          if (depth >= SpanDumpDepth::STRIPE) {
            Errata r = stripe->loadMeta();
            if (r) {
              // print Meta[A][HEAD]
              std::string MetaCopy[2] = {"A", "B"};
              std::string MetaType[2] = {"HEAD", "FOOT"};
              for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                  std::cout << "\n" << MetaCopy[i] << ":" << MetaType[j] << "\n" << std::endl;
                  std::cout << " Magic:" << stripe->_meta[i][j].magic
                            << "\n version: ink_major: " << stripe->_meta[i][j].version.ink_major
                            << "\n version: ink_minor: " << stripe->_meta[i][j].version.ink_minor
                            << "\n create_time: " << stripe->_meta[i][j].create_time
                            << "\n write_pos: " << stripe->_meta[i][j].write_pos
                            << "\n last_write_pos: " << stripe->_meta[i][j].last_write_pos
                            << "\n agg_pos: " << stripe->_meta[i][j].agg_pos << "\n generation: " << stripe->_meta[i][j].generation
                            << "\n phase: " << stripe->_meta[i][j].phase << "\n cycle: " << stripe->_meta[i][j].cycle
                            << "\n sync_serial: " << stripe->_meta[i][j].sync_serial
                            << "\n write_serial: " << stripe->_meta[i][j].write_serial << "\n dirty: " << stripe->_meta[i][j].dirty
                            << "\n sector_size: " << stripe->_meta[i][j].sector_size << std::endl;
                }
              }
              if (!stripe->validate_sync_serial()) {
                std::cout << "WARNING:::::Validity check failed for sync_serials" << std::endl;
              }
              stripe->_directory.clear();
            } else {
              std::cout << r;
            }
          }
        }
      }
    }
  }
}

void
Cache::dumpVolumes()
{
  for (auto const &elt : _volumes) {
    size_t size = 0;
    for (auto const &r : elt.second._stripes)
      size += r->_len;

    std::cout << "Volume " << elt.first << " has " << elt.second._stripes.size() << " stripes and " << size << " bytes"
              << std::endl;
  }
}

ts::CacheStripeBlocks
Cache::calcTotalSpanConfiguredSize()
{
  ts::CacheStripeBlocks zret(0);

  for (auto span : _spans) {
    zret += round_down(span->_len);
  }
  return zret;
}

#if 0
ts::CacheStripeBlocks
Cache::calcTotalSpanPhysicalSize()
{
  ts::CacheStripeBlocks zret(0);

  for (auto span : _spans) {
    // This is broken, physical_size doesn't work for devices, need to fix that.
    zret += round_down(span->_path.physical_size());
  }
  return zret;
}
#endif

Cache::~Cache()
{
  for (auto *span : _spans)
    delete span;
}
/* --------------------------------------------------------------------------------------- */
#if 0
Errata
Span::Initialize()
{
  Errata zret;

  for (auto *strp : _stripes) {
    std::cout << "strpe start=============" << strp->_start << std::endl;
  }
  auto skip = CacheStoreBlocks::SCALE;
  // successive approximation to calculate start
  auto start = skip;
  uint64_t l;
  for (int i = 0; i < 3; i++) {
    l = _len - (start - skip);
    if (l >= MIN_VOL_SIZE) {
      header_len = sizeof(ts::SpanHeader) + (l / MIN_VOL_SIZE - 1) * sizeof(CacheStripeDescriptor);
    } else {
      header_len = sizeof(ts::SpanHeader);
    }
    start = skip + header_len;
  }
  // num_usable_blocks = ((_len * STORE_BLOCK_SIZE) - (start - skip)) >> STORE_BLOCK_SHIFT;
  header_len       = ROUND_TO_STORE_BLOCK(header_len);
  std::cout << "start " << start << "len " << _len << " header length " << header_len << std::endl;
  num_usable_blocks = (_len - (start - skip)) >> STORE_BLOCK_SHIFT;
  return zret;
}
#endif

#if 0
Errata
Span::create_stripe(int number, off_t size_in_blocks, int scheme)
{
  Errata zret;
  // initialize span header first
  ts::SpanHeader sph;
  sph.magic            = ts::SpanHeader::MAGIC;
  sph.num_volumes      = 1;
  sph.num_free         = 1;
  sph.num_used         = 0;
  sph.num_diskvol_blks = -1;
  // sph.num_blocks = 0;

  // initialize stripes
  return zret;
}
#endif

Errata
Span::load()
{
  Errata zret;
  if (!_path.is_readable())
    zret = Errata::Message(0, EPERM, _path, " is not readable.");
  else if (_path.is_char_device() || _path.is_block_device())
    zret = this->loadDevice();
  else if (_path.is_dir())
    zret.push(0, 1, "Directory support not yet available");
  else
    zret.push(0, EBADF, _path, " is not a valid file type");
  return zret;
}

Errata
Span::loadDevice()
{
  Errata zret;
  int flags;

  flags = OPEN_RW_FLAG
#if defined(O_DIRECT)
          | O_DIRECT
#endif
#if defined(O_DSYNC)
          | O_DSYNC
#endif
    ;

  ats_scoped_fd fd{_path.open(flags)};

  if (fd) {
    if (ink_file_get_geometry(fd, _geometry)) {
      off_t offset = ts::CacheSpan::OFFSET;
      CacheStoreBlocks span_hdr_size(1);                        // default.
      static const ssize_t BUFF_SIZE = CacheStoreBlocks::SCALE; // match default span_hdr_size
      alignas(512) char buff[BUFF_SIZE];
      ssize_t n = pread(fd, buff, BUFF_SIZE, offset);
      if (n >= BUFF_SIZE) {
        ts::SpanHeader &span_hdr = reinterpret_cast<ts::SpanHeader &>(buff);
        _base                    = round_up(offset);
        // See if it looks valid
        if (span_hdr.magic == ts::SpanHeader::MAGIC && span_hdr.num_diskvol_blks == span_hdr.num_used + span_hdr.num_free) {
          int nspb      = span_hdr.num_diskvol_blks;
          span_hdr_size = round_up(sizeof(ts::SpanHeader) + (nspb - 1) * sizeof(ts::CacheStripeDescriptor));
          _header.reset(new (malloc(span_hdr_size)) ts::SpanHeader);
          if (span_hdr_size <= BUFF_SIZE) {
            memcpy(_header.get(), buff, span_hdr_size);
          } else {
            // TODO - check the pread return
            pread(fd, _header.get(), span_hdr_size, offset);
          }
          _len = _header->num_blocks;
        } else {
          zret = Errata::Message(0, 0, "Span header for ", _path, " is invalid");
          _len = round_down(_geometry.totalsz) - _base;
        }
        // valid FD means the device is accessible and has enough storage to be configured.
        _fd     = fd.release();
        _offset = _base + span_hdr_size;
      } else {
        zret = Errata::Message(0, errno, "Failed to read from ", _path, '[', errno, ':', strerror(errno), ']');
      }
    } else {
      zret = Errata::Message(0, 23, "Unable to get device geometry for ", _path);
    }
  } else {
    zret = Errata::Message(0, errno, "Unable to open ", _path);
  }
  return zret;
}

ts::Rv<Stripe *>
Span::allocStripe(int vol_idx, CacheStripeBlocks len)
{
  for (auto spot = _stripes.begin(), limit = _stripes.end(); spot != limit; ++spot) {
    Stripe *stripe = *spot;
    if (stripe->isFree()) {
      if (len < stripe->_len) {
        // If the remains would be less than a stripe block, just take it all.
        if (stripe->_len <= (len + CacheStripeBlocks(1))) {
          stripe->_vol_idx = vol_idx;
          stripe->_type    = 1;
          return stripe;
        } else {
          Stripe *ns = new Stripe(this, stripe->_start, len);
          stripe->_start += len;
          stripe->_len -= len;
          ns->_vol_idx = vol_idx;
          ns->_type    = 1;
          _stripes.insert(spot, ns);
          return ns;
        }
      }
    }
  }
  return ts::Rv<Stripe *>(nullptr,
                          Errata::Message(0, 15, "Failed to allocate stripe of size ", len, " - no free block large enough"));
}

bool
Span::isEmpty() const
{
  return std::all_of(_stripes.begin(), _stripes.end(), [](Stripe *s) { return s->_vol_idx == 0; });
}

Errata
Span::clear()
{
  Stripe *stripe;
  std::for_each(_stripes.begin(), _stripes.end(), [](Stripe *s) { delete s; });
  _stripes.clear();

  // Gah, due to lack of anything better, TS depends on the number of usable blocks to be consistent
  // with internal calculations so have to match that here. Yay.
  CacheStoreBlocks eff = _len - _base; // starting # of usable blocks.
  // The maximum number of volumes that can store stored, accounting for the space used to store the descriptors.
  int n   = (eff - sizeof(ts::SpanHeader)) / (CacheStripeBlocks::SCALE + sizeof(CacheStripeDescriptor));
  _offset = _base + round_up(sizeof(ts::SpanHeader) + (n - 1) * sizeof(CacheStripeDescriptor));
  stripe  = new Stripe(this, _offset, _len - _offset);
  stripe->vol_init_data();
  stripe->InitializeMeta();
  _stripes.push_back(stripe);
  _free_space = stripe->_len;

  return Errata();
}

Errata
Span::updateHeader()
{
  Errata zret;
  int n = _stripes.size();
  CacheStripeDescriptor *sd;
  CacheStoreBlocks hdr_size = round_up(sizeof(ts::SpanHeader) + (n - 1) * sizeof(ts::CacheStripeDescriptor));
  void *raw                 = ats_memalign(512, hdr_size);
  ts::SpanHeader *hdr       = static_cast<ts::SpanHeader *>(raw);
  std::bitset<ts::MAX_VOLUME_IDX + 1> volume_mask;

  hdr->magic            = ts::SpanHeader::MAGIC;
  hdr->num_free         = 0;
  hdr->num_used         = 0;
  hdr->num_diskvol_blks = n;
  hdr->num_blocks       = _len;

  sd = hdr->stripes;
  for (auto stripe : _stripes) {
    sd->offset               = stripe->_start;
    sd->len                  = stripe->_len;
    sd->vol_idx              = stripe->_vol_idx;
    sd->type                 = stripe->_type;
    volume_mask[sd->vol_idx] = true;
    if (sd->vol_idx == 0) {
      sd->free = true;
      ++(hdr->num_free);
    } else {
      sd->free = false;
      ++(hdr->num_used);
    }

    ++sd;
  }
  volume_mask[0]   = false; // don't include free stripes in distinct volume count.
  hdr->num_volumes = volume_mask.count();
  _header.reset(hdr);
  if (OPEN_RW_FLAG) {
    ssize_t r = pwrite(_fd, hdr, hdr_size, ts::CacheSpan::OFFSET);
    if (r < ts::CacheSpan::OFFSET)
      zret.push(0, errno, "Failed to update span - ", strerror(errno));
  } else {
    std::cout << "Writing not enabled, no updates perfomed" << std::endl;
  }
  return zret;
}

void
Span::clearPermanently()
{
  if (OPEN_RW_FLAG) {
    alignas(512) static char zero[CacheStoreBlocks::SCALE]; // should be all zero, it's static.
    std::cout << "Clearing " << _path << " permanently on disk ";
    ssize_t n = pwrite(_fd, zero, sizeof(zero), ts::CacheSpan::OFFSET);
    if (n == sizeof(zero))
      std::cout << "done";
    else {
      const char *text = strerror(errno);
      std::cout << "failed";
      if (n >= 0)
        std::cout << " - " << n << " of " << sizeof(zero) << " bytes written";
      std::cout << " - " << text;
    }

    std::cout << std::endl;
    // clear the stripes as well
    for (auto *strp : _stripes) {
      strp->loadMeta();
      std::cout << "Clearing stripe @" << strp->_start << " of length: " << strp->_len << std::endl;
      strp->clear();
    }
  } else {
    std::cout << "Clearing " << _path << " not performed, write not enabled" << std::endl;
  }
}

// explicit pair for random table in build_vol_hash_table
struct rtable_pair {
  unsigned int rval; ///< relative value, used to sort.
  unsigned int idx;  ///< volume mapping table index.
};

// comparison operator for random table in build_vol_hash_table
// sorts based on the randomly assigned rval
static int
cmprtable(const void *aa, const void *bb)
{
  rtable_pair *a = (rtable_pair *)aa;
  rtable_pair *b = (rtable_pair *)bb;
  if (a->rval < b->rval) {
    return -1;
  }
  if (a->rval > b->rval) {
    return 1;
  }
  return 0;
}

unsigned int
next_rand(unsigned int *p)
{
  unsigned int seed = *p;
  seed              = 1103515145 * seed + 12345;
  *p                = seed;
  return seed;
}

void
Cache::build_stripe_hash_table()
{
  int num_stripes = globalVec_stripe.size();
  CacheStoreBlocks total;
  unsigned int *forvol         = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned int *gotvol         = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned int *rnd            = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned short *ttable       = (unsigned short *)ats_malloc(sizeof(unsigned short) * VOL_HASH_TABLE_SIZE);
  unsigned int *rtable_entries = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned int rtable_size     = 0;
  int i                        = 0;
  uint64_t used                = 0;
  // estimate allocation
  for (auto &elt : globalVec_stripe) {
    // printf("stripe length %" PRId64 "\n", elt->_len.count());
    rtable_entries[i] = static_cast<int64_t>(elt->_len) / Vol_hash_alloc_size;
    rtable_size += rtable_entries[i];
    uint64_t x = elt->hash_id.fold();
    // seed random number generator
    rnd[i] = (unsigned int)x;
    total += elt->_len;
    i++;
  }
  i = 0;
  for (auto &elt : globalVec_stripe) {
    forvol[i] = static_cast<int64_t>(VOL_HASH_TABLE_SIZE * elt->_len) / total;
    used += forvol[i];
    gotvol[i] = 0;
    i++;
  }

  // spread around the excess
  int extra = VOL_HASH_TABLE_SIZE - used;
  for (int i = 0; i < extra; i++) {
    forvol[i % num_stripes]++;
  }

  // initialize table to "empty"
  for (int i = 0; i < VOL_HASH_TABLE_SIZE; i++) {
    ttable[i] = VOL_HASH_EMPTY;
  }

  // generate random numbers proportaion to allocation
  rtable_pair *rtable = (rtable_pair *)ats_malloc(sizeof(rtable_pair) * rtable_size);
  int rindex          = 0;
  for (int i = 0; i < num_stripes; i++) {
    for (int j = 0; j < (int)rtable_entries[i]; j++) {
      rtable[rindex].rval = next_rand(&rnd[i]);
      rtable[rindex].idx  = i;
      rindex++;
    }
  }
  assert(rindex == (int)rtable_size);
  // sort (rand #, vol $ pairs)
  qsort(rtable, rtable_size, sizeof(rtable_pair), cmprtable);
  unsigned int width = (1LL << 32) / VOL_HASH_TABLE_SIZE;
  unsigned int pos; // target position to allocate
  // select vol with closest random number for each bucket
  i = 0; // index moving through the random numbers
  for (int j = 0; j < VOL_HASH_TABLE_SIZE; j++) {
    pos = width / 2 + j * width; // position to select closest to
    while (pos > rtable[i].rval && i < (int)rtable_size - 1) {
      i++;
    }
    ttable[j] = rtable[i].idx;
    gotvol[rtable[i].idx]++;
  }
  for (int i = 0; i < num_stripes; i++) {
    printf("build_vol_hash_table index %d mapped to %d requested %d got %d\n", i, i, forvol[i], gotvol[i]);
  }
  stripes_hash_table = ttable;

  ats_free(forvol);
  ats_free(gotvol);
  ats_free(rnd);
  ats_free(rtable_entries);
  ats_free(rtable);
}

Stripe *
Cache::key_to_stripe(INK_MD5 *key, const char *hostname, int host_len)
{
  uint32_t h = (key->slice32(2) >> DIR_TAG_WIDTH) % VOL_HASH_TABLE_SIZE;
  return globalVec_stripe[stripes_hash_table[h]];
}

/* --------------------------------------------------------------------------------------- */
Errata
VolumeConfig::load(FilePath const &path)
{
  static const ts::TextView TAG_SIZE("size");
  static const ts::TextView TAG_VOL("volume");

  Errata zret;

  int ln = 0;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::TextView content = cfile.content();
    while (content) {
      Data v;

      ++ln;
      ts::TextView line = content.take_prefix_at('\n');
      line.ltrim_if(&isspace);
      if (line.empty() || '#' == *line)
        continue;

      while (line) {
        ts::TextView value(line.take_prefix_if(&isspace));
        ts::TextView tag(value.take_prefix_at('='));
        if (tag.empty()) {
          zret.push(0, 1, "Line ", ln, " is invalid");
        } else if (0 == strcasecmp(tag, TAG_SIZE)) {
          if (v.hasSize()) {
            zret.push(0, 5, "Line ", ln, " has field ", TAG_SIZE, " more than once");
          } else {
            ts::TextView text;
            auto n = ts::svtoi(value, &text);
            if (text) {
              ts::TextView percent(text.data_end(), value.data_end()); // clip parsed number.
              if (percent.empty()) {
                v._size = CacheStripeBlocks(round_up(Megabytes(n)));
                if (v._size.count() != n) {
                  zret.push(0, 0, "Line ", ln, " size ", n, " was rounded up to ", v._size);
                }
              } else if ('%' == *percent && percent.size() == 1) {
                v._percent = n;
              } else {
                zret.push(0, 3, "Line ", ln, " has invalid value '", value, "' for ", TAG_SIZE, " field");
              }
            } else {
              zret.push(0, 2, "Line ", ln, " has invalid value '", value, "' for ", TAG_SIZE, " field");
            }
          }
        } else if (0 == strcasecmp(tag, TAG_VOL)) {
          if (v.hasIndex()) {
            zret.push(0, 6, "Line ", ln, " has field ", TAG_VOL, " more than once");
          } else {
            ts::TextView text;
            auto n = ts::svtoi(value, &text);
            if (text == value) {
              v._idx = n;
            } else {
              zret.push(0, 4, "Line ", ln, " has invalid value '", value, "' for ", TAG_VOL, " field");
            }
          }
        }
      }
      if (v.hasSize() && v.hasIndex()) {
        _volumes.push_back(std::move(v));
      } else {
        if (!v.hasSize())
          zret.push(0, 7, "Line ", ln, " does not have the required field ", TAG_SIZE);
        if (!v.hasIndex())
          zret.push(0, 8, "Line ", ln, " does not have the required field ", TAG_VOL);
      }
    }
  } else {
    zret = Errata::Message(0, EBADF, "Unable to load ", path);
  }
  return zret;
}
/* --------------------------------------------------------------------------------------- */
struct option Options[] = {{"help", 0, nullptr, 'h'},  {"spans", 1, nullptr, 's'}, {"volumes", 1, nullptr, 'v'},
                           {"write", 0, nullptr, 'w'}, {"input", 1, nullptr, 'i'}, {"device", 1, nullptr, 'd'},
                           {"aos", 1, nullptr, 'o'},   {nullptr, 0, nullptr, 0}};
}

Errata
List_Stripes(Cache::SpanDumpDepth depth)
{
  Errata zret;
  Cache cache;

  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(depth);
    cache.dumpVolumes();
  }
  return zret;
}

Errata
Cmd_Allocate_Empty_Spans(int argc, char *argv[])
{
  Errata zret;
  VolumeAllocator va;

  //  OPEN_RW_FLAG = O_RDWR;
  zret = va.load(SpanFile, VolumeFile);
  if (zret) {
    va.fillEmptySpans();
  }

  return zret;
}

Errata
Simulate_Span_Allocation(int argc, char *argv[])
{
  Errata zret;
  VolumeAllocator va;

  if (!VolumeFile)
    zret.push(0, 9, "Volume config file not set");
  if (!SpanFile)
    zret.push(0, 9, "Span file not set");

  if (zret) {
    if ((zret = va.load(SpanFile, VolumeFile)).isOK()) {
      zret = va.fillAllSpans();
      va.dumpVolumes();
    }
  }
  return zret;
}

Errata
Clear_Spans(int argc, char *argv[])
{
  Errata zret;

  Cache cache;
  //  OPEN_RW_FLAG = O_RDWR;
  if ((zret = cache.loadSpan(SpanFile))) {
    for (auto *span : cache._spans) {
      span->clearPermanently();
    }
  }

  return zret;
}

Errata
Find_Stripe(FilePath const &input_file_path)
{
  // scheme=http user=u password=p host=172.28.56.109 path=somepath query=somequery port=1234
  // input file format: scheme://hostname:port/somepath;params?somequery user=USER password=PASS
  // user, password, are optional; scheme and host are required

  //  char* h= http://user:pass@IPADDRESS/path_to_file;?port      <== this is the format we need
  //  url_matcher matcher;
  //  if (matcher.match(h))
  //    std::cout << h << " : is valid" << std::endl;
  //  else
  //    std::cout << h << " : is NOT valid" << std::endl;

  Errata zret;
  Cache cache;
  if (input_file_path)
    printf("passed argv %s\n", input_file_path.path());
  cache.loadURLs(input_file_path);
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    cache.build_stripe_hash_table();
    for (auto host : cache.URLset) {
      MD5Context ctx;
      INK_MD5 hashT;
      ts::LocalBufferWriter<33> w;
      ctx.update(host->url.data(), host->url.size());
      ctx.update(&host->port, sizeof(host->port));
      ctx.finalize(hashT);
      Stripe *stripe_ = cache.key_to_stripe(&hashT, host->url.data(), host->url.size());
      w.print("{}", hashT);
      printf("hash of %.*s is %.*s: Stripe  %s \n", (int)host->url.size(), host->url.data(), static_cast<int>(w.size()), w.data(),
             stripe_->hashText.data());
    }
  }

  return zret;
}

Errata
dir_check()
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto &stripe : cache.globalVec_stripe) {
      stripe->dir_check();
    }
  }
  printf("\nCHECK succeeded\n");
  return zret;
}

Errata
walk_bucket_chain(std::string devicePath)
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto sp : cache._spans) {
      if (devicePath.size() > 0 && 0 == strncmp(sp->_path.path(), devicePath.data(), devicePath.size())) {
        for (auto strp : sp->_stripes) {
          strp->loadMeta();
          strp->loadDir();
          strp->walk_all_buckets();
        }
      }
    }
  }
  return zret;
}

Errata
Clear_Span(std::string devicePath)
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto sp : cache._spans) {
      if (devicePath.size() > 0 && 0 == strncmp(sp->_path.path(), devicePath.data(), devicePath.size())) {
        printf("clearing %s\n", devicePath.data());
        sp->clearPermanently();
      }
    }
  }
  return zret;
}

Errata
Check_Freelist(std::string devicePath)
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto sp : cache._spans) {
      if (devicePath.size() > 0 && 0 == strncmp(sp->_path.path(), devicePath.data(), devicePath.size())) {
        printf("Scanning %s\n", devicePath.data());
        for (auto strp : sp->_stripes) {
          strp->loadMeta();
          strp->loadDir();
          /* what is this trying to do?
          for (int s = 0; s < strp->_segments; s++) {
            int freelist = strp->dir_freelist_length(s);
          }
         */
        }
        break;
      }
    }
  }
  return zret;
}

Errata
Init_disk(FilePath const &input_file_path)
{
  Errata zret;
  Cache cache;
  VolumeAllocator va;

  //  OPEN_RW_FLAG = O_RDWR;
  zret = va.load(SpanFile, VolumeFile);
  va.allocateSpan(input_file_path);

  return zret;
}

Errata
Get_Response(FilePath const &input_file_path)
{
  // scheme=http user=u password=p host=172.28.56.109 path=somepath query=somequery port=1234
  // input file format: scheme://hostname:port/somepath;params?somequery user=USER password=PASS
  // user, password, are optional; scheme and host are required

  //  char* h= http://user:pass@IPADDRESS/path_to_file;?port      <== this is the format we need
  //  url_matcher matcher;
  //  if (matcher.match(h))
  //    std::cout << h << " : is valid" << std::endl;
  //  else
  //    std::cout << h << " : is NOT valid" << std::endl;

  Errata zret;
  Cache cache;
  if (input_file_path)
    printf("passed argv %s\n", input_file_path.path());
  cache.loadURLs(input_file_path);
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    cache.build_stripe_hash_table();
    for (auto host : cache.URLset) {
      MD5Context ctx;
      INK_MD5 hashT;
      ts::LocalBufferWriter<33> w;
      ctx.update(host->url.data(), host->url.size());
      ctx.update(&host->port, sizeof(host->port));
      ctx.finalize(hashT);
      Stripe *stripe_ = cache.key_to_stripe(&hashT, host->url.data(), host->url.size());
      w.print("{}", hashT);
      printf("hash of %.*s is %.*s: Stripe  %s \n", (int)host->url.size(), host->url.data(), static_cast<int>(w.size()), w.data(),
             stripe_->hashText.data());
      CacheDirEntry *dir_result = nullptr;
      stripe_->loadMeta();
      stripe_->loadDir();
      stripe_->dir_probe(&hashT, dir_result, nullptr);
    }
  }

  return zret;
}

int
main(int argc, char *argv[])
{
  int opt_idx = 0;
  int opt_val;
  bool help = false;
  FilePath input_url_file;
  std::string inputFile;
  char *inp = nullptr;
  while (-1 != (opt_val = getopt_long(argc, argv, "h", Options, &opt_idx))) {
    switch (opt_val) {
    case 'h':
      printf("Usage: %s --span <SPAN> --volume <FILE> <COMMAND> [<SUBCOMMAND> ...]\n", argv[0]);
      help = true;
      break;
    case 's':
      SpanFile = optarg;
      break;
    case 'v':
      VolumeFile = optarg;
      break;
    case 'w':
      OPEN_RW_FLAG = O_RDWR;
      std::cout << "NOTE: Writing to physical devices enabled" << std::endl;
      break;
    case 'i':
      input_url_file = optarg;
      break;
    case 'o':
      cache_config_min_average_object_size = atoi(optarg);
      break;
    case 'd':
      if (!inp) {
        inp = strdup(optarg);
        inputFile.assign(inp, strlen(inp));
      }
      break;
    }
  }

  Commands.add("list", "List elements of the cache", []() { return List_Stripes(Cache::SpanDumpDepth::SPAN); })
    .subCommand(std::string("stripes"), std::string("List the stripes"),
                []() { return List_Stripes(Cache::SpanDumpDepth::STRIPE); });
  Commands.add(std::string("clear"), std::string("Clear spans"), &Clear_Spans);
  auto &c = Commands.add(std::string("dir_check"), std::string("cache check"));
  c.subCommand(std::string("full"), std::string("Full report of the cache storage"), &dir_check);
  c.subCommand(std::string("freelist"), std::string("check the freelist for loop"),
               [&](int, char *argv[]) { return Check_Freelist(inputFile); });
  c.subCommand(std::string("bucket_chain"), std::string("walk bucket chains for loops"),
               [&](int, char *argv[]) { return walk_bucket_chain(inputFile); });
  Commands.add(std::string("volumes"), std::string("Volumes"), &Simulate_Span_Allocation);
  Commands.add(std::string("alloc"), std::string("Storage allocation"))
    .subCommand(std::string("free"), std::string("Allocate storage on free (empty) spans"), &Cmd_Allocate_Empty_Spans);
  Commands.add(std::string("find"), std::string("Find Stripe Assignment"))
    .subCommand(std::string("url"), std::string("URL"), [&](int, char *argv[]) { return Find_Stripe(input_url_file); });
  Commands.add(std::string("clearspan"), std::string("clear specific span"))
    .subCommand(std::string("span"), std::string("device path"), [&](int, char *argv[]) { return Clear_Span(inputFile); });
  Commands.add(std::string("retrieve"), std::string(" retrieve the response of the given list of URLs"),
               [&](int, char *argv[]) { return Get_Response(input_url_file); });
  Commands.add(std::string("init"), std::string(" Initialize Span if the header is invalid"),
               [&](int, char *argv[]) { return Init_disk(input_url_file); });
  Commands.setArgIndex(optind);

  if (help) {
    Commands.helpMessage(argc - 1, argv + 1);
    exit(1);
  }

  Errata result = Commands.invoke(argc, argv);

  if (result.size()) {
    std::cerr << result;
  }
  if (inp)
    free(inp);
  return 0;
}
