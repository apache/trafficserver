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

#include "P_CacheHttp.h"
#include "iocore/cache/CacheDefs.h"
#include "iocore/eventsystem/Continuation.h"
#include "iocore/aio/AIO.h"
#include "tscore/Version.h"

#include <cstdint>
#include <ctime>

class Stripe;
class StripeSM;
struct InterimCacheVol;
struct CacheVC;
class CacheEvacuateDocVC;

// #define LOOP_CHECK_MODE 1

/*
  Directory layout
*/

// Constants

#define DIR_TAG_WIDTH         12
#define DIR_MASK_TAG(_t)      ((_t) & ((1 << DIR_TAG_WIDTH) - 1))
#define SIZEOF_DIR            10
#define ESTIMATED_OBJECT_SIZE 8000

#define MAX_DIR_SEGMENTS        (32 * (1 << 16))
#define DIR_DEPTH               4
#define MAX_ENTRIES_PER_SEGMENT (1 << 16)
#define MAX_BUCKETS_PER_SEGMENT (MAX_ENTRIES_PER_SEGMENT / DIR_DEPTH)
#define DIR_SIZE_WIDTH          6
#define DIR_BLOCK_SIZES         4
#define DIR_BLOCK_SHIFT(_i)     (3 * (_i))
#define DIR_BLOCK_SIZE(_i)      (CACHE_BLOCK_SIZE << DIR_BLOCK_SHIFT(_i))
#define DIR_SIZE_WITH_BLOCK(_i) ((1 << DIR_SIZE_WIDTH) * DIR_BLOCK_SIZE(_i))
#define DIR_OFFSET_BITS         40
#define DIR_OFFSET_MAX          ((((off_t)1) << DIR_OFFSET_BITS) - 1)

#define DO_NOT_REMOVE_THIS 0

// Debugging Options

// #define DO_CHECK_DIR_FAST
// #define DO_CHECK_DIR

// Macros

#ifdef DO_CHECK_DIR
#define CHECK_DIR(_d) ink_assert(_d->check())
#else
#define CHECK_DIR(_d) ((void)0)
#endif

#define dir_index(_e, _i) ((Dir *)((char *)(_e)->directory.dir + (SIZEOF_DIR * (_i))))
#define dir_assign(_e, _x)   \
  do {                       \
    (_e)->w[0] = (_x)->w[0]; \
    (_e)->w[1] = (_x)->w[1]; \
    (_e)->w[2] = (_x)->w[2]; \
    (_e)->w[3] = (_x)->w[3]; \
    (_e)->w[4] = (_x)->w[4]; \
  } while (0)
#define dir_assign_data(_e, _x)         \
  do {                                  \
    unsigned short next = dir_next(_e); \
    dir_assign(_e, _x);                 \
    dir_set_next(_e, next);             \
  } while (0)
#define dir_is_empty(_e) (!dir_offset(_e))
#define dir_clear(_e) \
  do {                \
    (_e)->w[0] = 0;   \
    (_e)->w[1] = 0;   \
    (_e)->w[2] = 0;   \
    (_e)->w[3] = 0;   \
    (_e)->w[4] = 0;   \
  } while (0)
#define dir_clean(_e) dir_set_offset(_e, 0)

// OpenDir

#define OPEN_DIR_BUCKETS 256

struct EvacuationBlock;

// Cache Directory

// INTERNAL: do not access these members directly, use the
// accessors below (e.g. dir_offset, dir_set_offset).
// These structures are stored in memory 2 byte aligned.
// The accessors prevent unaligned memory access which
// is often either less efficient or unsupported depending
// on the processor.
struct Dir {
#if DO_NOT_REMOVE_THIS
  // THE BIT-FIELD INTERPRETATION OF THIS STRUCT WHICH HAS TO
  // USE MACROS TO PREVENT UNALIGNED LOADS
  // bits are numbered from lowest in u16 to highest
  // always index as u16 to avoid byte order issues
  unsigned int offset      : 24; // (0,1:0-7) 16M * 512 = 8GB
  unsigned int big         : 2;  // (1:8-9) 512 << (3 * big)
  unsigned int size        : 6;  // (1:10-15) 6**2 = 64, 64*512 = 32768 .. 64*256=16MB
  unsigned int tag         : 12; // (2:0-11) 2048 / 8 entries/bucket = .4%
  unsigned int phase       : 1;  // (2:12)
  unsigned int head        : 1;  // (2:13) first segment in a document
  unsigned int pinned      : 1;  // (2:14)
  unsigned int token       : 1;  // (2:15)
  unsigned int next        : 16; // (3)
  unsigned int offset_high : 16; // 8GB * 65k = 0.5PB (4)
#else
  uint16_t w[5];
  Dir() { dir_clear(this); }
#endif
};

#define dir_offset(_e) \
  ((int64_t)(((uint64_t)(_e)->w[0]) | (((uint64_t)((_e)->w[1] & 0xFF)) << 16) | (((uint64_t)(_e)->w[4]) << 24)))
#define dir_set_offset(_e, _o)                                              \
  do {                                                                      \
    (_e)->w[0] = (uint16_t)_o;                                              \
    (_e)->w[1] = (uint16_t)((((_o) >> 16) & 0xFF) | ((_e)->w[1] & 0xFF00)); \
    (_e)->w[4] = (uint16_t)((_o) >> 24);                                    \
  } while (0)
#define dir_bit(_e, _w, _b)         ((uint32_t)(((_e)->w[_w] >> (_b)) & 1))
#define dir_set_bit(_e, _w, _b, _v) (_e)->w[_w] = (uint16_t)(((_e)->w[_w] & ~(1 << (_b))) | (((_v) ? 1 : 0) << (_b)))
#define dir_big(_e)                 ((uint32_t)((((_e)->w[1]) >> 8) & 0x3))
#define dir_set_big(_e, _v)         (_e)->w[1] = (uint16_t)(((_e)->w[1] & 0xFCFF) | (((uint16_t)(_v)) & 0x3) << 8)
#define dir_size(_e)                ((uint32_t)(((_e)->w[1]) >> 10))
#define dir_set_size(_e, _v)        (_e)->w[1] = (uint16_t)(((_e)->w[1] & ((1 << 10) - 1)) | ((_v) << 10))
#define dir_set_approx_size(_e, _s)                     \
  do {                                                  \
    if ((_s) <= DIR_SIZE_WITH_BLOCK(0)) {               \
      dir_set_big(_e, 0);                               \
      dir_set_size(_e, ((_s) - 1) / DIR_BLOCK_SIZE(0)); \
    } else if ((_s) <= DIR_SIZE_WITH_BLOCK(1)) {        \
      dir_set_big(_e, 1);                               \
      dir_set_size(_e, ((_s) - 1) / DIR_BLOCK_SIZE(1)); \
    } else if ((_s) <= DIR_SIZE_WITH_BLOCK(2)) {        \
      dir_set_big(_e, 2);                               \
      dir_set_size(_e, ((_s) - 1) / DIR_BLOCK_SIZE(2)); \
    } else {                                            \
      dir_set_big(_e, 3);                               \
      dir_set_size(_e, ((_s) - 1) / DIR_BLOCK_SIZE(3)); \
    }                                                   \
  } while (0)
#define dir_approx_size(_e) ((dir_size(_e) + 1) * DIR_BLOCK_SIZE(dir_big(_e)))
#define round_to_approx_dir_size(_s)      \
  (_s <= DIR_SIZE_WITH_BLOCK(0) ?         \
     ROUND_TO(_s, DIR_BLOCK_SIZE(0)) :    \
     (_s <= DIR_SIZE_WITH_BLOCK(1) ?      \
        ROUND_TO(_s, DIR_BLOCK_SIZE(1)) : \
        (_s <= DIR_SIZE_WITH_BLOCK(2) ? ROUND_TO(_s, DIR_BLOCK_SIZE(2)) : ROUND_TO(_s, DIR_BLOCK_SIZE(3)))))
#define dir_tag(_e) ((uint32_t)((_e)->w[2] & ((1 << DIR_TAG_WIDTH) - 1)))
#define dir_set_tag(_e, _t) \
  (_e)->w[2] = (uint16_t)(((_e)->w[2] & ~((1 << DIR_TAG_WIDTH) - 1)) | ((_t) & ((1 << DIR_TAG_WIDTH) - 1)))
#define dir_phase(_e)          dir_bit(_e, 2, 12)
#define dir_set_phase(_e, _v)  dir_set_bit(_e, 2, 12, _v)
#define dir_head(_e)           dir_bit(_e, 2, 13)
#define dir_set_head(_e, _v)   dir_set_bit(_e, 2, 13, _v)
#define dir_pinned(_e)         dir_bit(_e, 2, 14)
#define dir_set_pinned(_e, _v) dir_set_bit(_e, 2, 14, _v)
// Bit 2:15 is unused.
#define dir_next(_e)         (_e)->w[3]
#define dir_set_next(_e, _o) (_e)->w[3] = (uint16_t)(_o)
#define dir_prev(_e)         (_e)->w[2]
#define dir_set_prev(_e, _o) (_e)->w[2] = (uint16_t)(_o)

// INKqa11166 - Cache can not store 2 HTTP alternates simultaneously.
// To allow this, move the vector from the CacheVC to the OpenDirEntry.
// Each CacheVC now maintains a pointer to this vector. Adding/Deleting
// alternates from this vector is done under the StripeSM::lock. The alternate
// is deleted/inserted into the vector just before writing the vector disk
// (CacheVC::updateVector).
LINK_FORWARD_DECLARATION(CacheVC, opendir_link) // forward declaration
struct OpenDirEntry {
  DLL<CacheVC, Link_CacheVC_opendir_link> writers; // list of all the current writers
  DLL<CacheVC, Link_CacheVC_opendir_link> readers; // list of all the current readers - not used
  CacheHTTPInfoVector                     vector;  // Vector for the http document. Each writer
                                                   // maintains a pointer to this vector and
                                                   // writes it down to disk.
  CacheKey single_doc_key;                         // Key for the resident alternate.
  Dir      single_doc_dir;                         // Directory for the resident alternate
  Dir      first_dir;                              // Dir for the vector. If empty, a new dir is
                                                   // inserted, otherwise this dir is overwritten
  uint16_t num_writers;                            // num of current writers
  uint16_t max_writers;                            // max number of simultaneous writers allowed
  bool     dont_update_directory;                  // if set, the first_dir is not updated.
  bool     move_resident_alt;                      // if set, single_doc_dir is inserted.
  bool     reading_vec;                            // somebody is currently reading the vector
  bool     writing_vec;                            // somebody is currently writing the vector

  LINK(OpenDirEntry, link);

  bool
  has_multiple_writers()
  {
    return num_writers > 1;
  }
};

struct OpenDir : public Continuation {
  Queue<CacheVC, Link_CacheVC_opendir_link> delayed_readers;
  DLL<OpenDirEntry>                         bucket[OPEN_DIR_BUCKETS];

  int           open_write(CacheVC *c, int allow_if_writers, int max_writers);
  int           close_write(CacheVC *c);
  OpenDirEntry *open_read(const CryptoHash *key) const;
  int           signal_readers(int event, Event *e);

  OpenDir();
};

struct CacheSync : public Continuation {
  int         stripe_index = 0;
  char       *buf          = nullptr;
  size_t      buflen       = 0;
  bool        buf_huge     = false;
  off_t       writepos     = 0;
  AIOCallback io;
  Event      *trigger    = nullptr;
  ink_hrtime  start_time = 0;
  int         mainEvent(int event, Event *e);
  void        aio_write(int fd, char *b, int n, off_t o);

  CacheSync() : Continuation(new_ProxyMutex()) { SET_HANDLER(&CacheSync::mainEvent); }
};

struct StripteHeaderFooter {
  unsigned int      magic;
  ts::VersionNumber version;
  time_t            create_time;
  off_t             write_pos;
  off_t             last_write_pos;
  off_t             agg_pos;
  uint32_t          generation; // token generation (vary), this cannot be 0
  uint32_t          phase;
  uint32_t          cycle;
  uint32_t          sync_serial;
  uint32_t          write_serial;
  uint32_t          dirty;
  uint32_t          sector_size;
  uint32_t          unused; // pad out to 8 byte boundary
  uint16_t          freelist[1];
};

struct Directory {
  char                *raw_dir{nullptr};
  Dir                 *dir{};
  StripteHeaderFooter *header{};
  StripteHeaderFooter *footer{};
  int                  segments{};
  off_t                buckets{};

  /* Total number of dir entries.
   */
  int entries() const;

  /* Returns the first dir in segment @a s.
   */
  Dir *get_segment(int s) const;

  int      probe(const CacheKey *, StripeSM *, Dir *, Dir **);
  int      insert(const CacheKey *key, StripeSM *stripe, Dir *to_part);
  int      overwrite(const CacheKey *key, StripeSM *stripe, Dir *to_part, Dir *overwrite, bool must_overwrite = true);
  int      remove(const CacheKey *key, StripeSM *stripe, Dir *del);
  void     free_entry(Dir *e, int s);
  int      check();
  void     cleanup(Stripe *stripe);
  void     clear_range(off_t start, off_t end, Stripe *stripe);
  uint64_t entries_used();
  int      bucket_length(Dir *b, int s);
  int      freelist_length(int s);
  void     clean_segment(int s, Stripe *stripe);
};

inline int
Directory::entries() const
{
  return this->buckets * DIR_DEPTH * this->segments;
}

inline Dir *
Directory::get_segment(int s) const
{
  return reinterpret_cast<Dir *>((reinterpret_cast<char *>(this->dir)) + (s * this->buckets) * DIR_DEPTH * SIZEOF_DIR);
}

// Global Functions

int  dir_lookaside_probe(const CacheKey *key, StripeSM *stripe, Dir *result, EvacuationBlock **eblock);
int  dir_lookaside_insert(EvacuationBlock *b, StripeSM *stripe, Dir *to);
int  dir_lookaside_fixup(const CacheKey *key, StripeSM *stripe);
void dir_lookaside_cleanup(StripeSM *stripe);
void dir_lookaside_remove(const CacheKey *key, StripeSM *stripe);
void dir_sync_init();
void sync_cache_dir_on_shutdown();

// Inline Functions

#define dir_in_seg(_s, _i) ((Dir *)(((char *)(_s)) + (SIZEOF_DIR * (_i))))

inline bool
dir_compare_tag(const Dir *e, const CacheKey *key)
{
  return (dir_tag(e) == DIR_MASK_TAG(key->slice32(2)));
}

inline Dir *
dir_from_offset(int64_t i, Dir *seg)
{
#if DIR_DEPTH < 5
  if (!i) {
    return nullptr;
  }
  return dir_in_seg(seg, i);
#else
  i = i + ((i - 1) / (DIR_DEPTH - 1));
  return dir_in_seg(seg, i);
#endif
}

inline Dir *
next_dir(Dir *d, Dir *seg)
{
  int i = dir_next(d);
  return dir_from_offset(i, seg);
}

inline int64_t
dir_to_offset(const Dir *d, const Dir *seg)
{
#if DIR_DEPTH < 5
  return (reinterpret_cast<const char *>(d) - reinterpret_cast<const char *>(seg)) / SIZEOF_DIR;
#else
  int64_t i = static_cast<int64_t>((reinterpret_cast<const char *>(d) - reinterpret_cast<const char *>(seg)) / SIZEOF_DIR);
  i         = i - (i / DIR_DEPTH);
  return i;
#endif
}

inline Dir *
dir_bucket(int64_t b, Dir *seg)
{
  return dir_in_seg(seg, b * DIR_DEPTH);
}

inline Dir *
dir_bucket_row(Dir *b, int64_t i)
{
  return dir_in_seg(b, i);
}
