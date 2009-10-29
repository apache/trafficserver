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


#ifndef _P_CACHE_DIR_H__
#define _P_CACHE_DIR_H__

#include "P_CacheHttp.h"

struct Part;
struct CacheVC;

// Constants

#define DIR_TAG_WIDTH			12
#define DIR_SIZE_WIDTH			6
#define DIR_MASK_TAG(_t)                ((_t) & ((1 << DIR_TAG_WIDTH) - 1))
#define DIR_MASK_TOKEN_TAG(_t)          ((_t) & ((1 << (DIR_TAG_WIDTH+DIR_SIZE_WIDTH)) - 1))
#define SIZEOF_DIR           		8
#define ESTIMATED_OBJECT_SIZE           8000


/* This number was changed from 16 to 32 so that we could support
   partititon size upt 8G ( 8 * 1024 * 1024 * 1024) with average
   object sizes as low as 4 KB.
*/
#define DIR_SEGMENTS                    32
 // between 2 (faster/more waste) and 5 (slower/less waste)
#define DIR_DEPTH                       4

#define DIR_SIZE_BIG                    32768
#define DIR_NULL                        0
#define DIR_OFFSET_BITS                 24
#define DIR_OFFSET_MAX                  ((1 << DIR_OFFSET_BITS) - 1)
#define DIR_OFFSET_MASK(_o)             ((_o) & DIR_OFFSET_MAX)


// Dir accessors: use these

#define dir_offset(_e) ((ink_off_t)(_e)->offset)
#define dir_set_offset(_e,_o) (_e)->offset = _o

#define dir_index(_e, _i) ((Dir*)((char*)(_e)->dir + (SIZEOF_DIR * (_i))))
#define round_to_approx_size(_s) \
(_s <= DIR_SIZE_BIG ? ROUND_TO_BLOCK(_s) : ROUND_TO_8K(_s))
#define dir_set_approx_size(_e, _s) do {                          \
  if (_s > DIR_SIZE_BIG) {                                        \
      (_e)->big_size = 1;                                         \
      (_e)->size = (((_s)-1) >> B8K_SHIFT);                       \
   } else {                                                       \
      (_e)->big_size = 0;                                         \
      (_e)->size = (((_s)-1) >> INK_BLOCK_SHIFT);                 \
    }                                                             \
} while (0)
#define dir_approx_size(_e)		         \
  ((_e)->big_size ?                              \
    (((_e)->size + 1) << B8K_SHIFT) :            \
    (((_e)->size + 1) << INK_BLOCK_SHIFT))
#define dir_phase(_e) (_e)->phase
#define dir_set_phase(_e,_p) (_e)->phase = _p
#define dir_tag(_e) (_e)->tag
#define dir_token(_e) (_e)->token
#define dir_set_token(_e, _v) (_e)->token = _v
#define dir_set_tag(_e,_t) (_e)->tag = _t
#define dir_assign(_e,_x) do {                \
  ((inku32*)(_e))[0] = ((inku32*)(_x))[0];    \
  ((inku32*)(_e))[1] = ((inku32*)(_x))[1];    \
} while (0)
#define dir_assign_data(_e,_x) do {           \
  unsigned short next = (_e)->next;           \
  dir_assign(_e, _x);		              \
  (_e)->next = next;                          \
} while(0)
// entry is valid
#define dir_valid(_d, _e)                                                \
  (_d->header->phase == dir_phase(_e) ? part_in_phase_valid(_d, _e) :    \
                                        part_out_of_phase_valid(_d, _e))
// entry is valid and outside of write aggregation region
#define dir_agg_valid(_d, _e)                                            \
  (_d->header->phase == dir_phase(_e) ? part_in_phase_valid(_d, _e) :    \
                                        part_out_of_phase_agg_valid(_d, _e))
// entry may be valid or overwritten in the last aggregated write
#define dir_write_valid(_d, _e)                                            \
  (_d->header->phase == dir_phase(_e) ? part_in_phase_valid(_d, _e) :    \
                                        part_out_of_phase_write_valid(_d, _e))
#define dir_agg_buf_valid(_d, _e)                                          \
  (_d->header->phase == dir_phase(_e) && part_in_phase_agg_buf_valid(_d, _e))


#define dir_set_dirinfo(_e, _i) *(DirInfo*)(_e) = *(DirInfo*)_i
#define dir_dirinfo(_e) (*(DirInfo*)(_e))
#define dir_next(_e) (_e)->next
#define dir_set_next(_e, _o) (_e)->next = _o
#define dir_prev(_e) ((FreeDir*)(_e))->prev
#define dir_set_prev(_e,_o) ((FreeDir*)(_e))->prev = _o
#define dir_head(_e) (_e)->head
#define dir_set_head(_e, _v) (_e)->head = _v
#define dir_pinned(_e) (_e)->pinned
#define dir_set_pinned(_e, _v) (_e)->pinned = _v
#define dir_is_empty(_e) (!(_e)->offset)
#define dir_clear(_e)  *((inku64*)(_e)) = 0
#define dir_clean(_e)  *((inku32*)(_e)) = 0

#if 1
#define dir_segment(_s, _d) (_d)->segment[_s]
#else
#define dir_segment(_s, _d) part_dir_segment(_d, _s)    // slower?
#endif

// DirInfo

#define dirinfo_clear(_x) (_x) = 0
#define dirinfo_is_empty(_x) (!(_x))

// OpenDir

#define OPEN_DIR_BUCKETS           256

struct EvacuationBlock;
typedef inku32 DirInfo;
// Global Data

// Cache Directory

// INTERNAL: do not access these members directly, use the
// accessors below (e.g. dir_offset, dir_set_offset)
struct Dir
{
  unsigned int offset:24;       // 16M * 512 = 8GB
  unsigned int big_size:1;
  unsigned int size:6;          // 64k/512 = 128 log2 = 7
  unsigned int phase:1;
  unsigned int tag:12;          // 2048 / 8 entries/bucket = .4%
  unsigned int head:1;          // first segment in a document
  unsigned int pinned:1;
  unsigned int token:1;
  unsigned int reserved:1;
  unsigned int next:16;
};

// INTERNAL: do not access these members directly, use the
// accessors below (e.g. dir_offset, dir_set_offset)
struct FreeDir
{
  unsigned int offset:24;       // 0: empty
  unsigned int reserved:8;
  unsigned int prev:16;
  unsigned int next:16;
};

// INKqa11166 - Cache can not store 2 HTTP alternates simultaneously.
// To allow this, move the vector from the CacheVC to the OpenDirEntry.
// Each CacheVC now maintains a pointer to this vector. Adding/Deleting
// alternates from this vector is done under the Part::lock. The alternate
// is deleted/inserted into the vector just before writing the vector disk 
// (CacheVC::updateVector). 
struct OpenDirEntry
{
  DLL<CacheVC> writers;      // list of all the current writers
  DLL<CacheVC> readers;      // list of all the current readers - not used
  CacheHTTPInfoVector vector;   // Vector for the http document. Each writer 
  // maintains a pointer to this vector and 
  // writes it down to disk. 
  CacheKey single_doc_key;      // Key for the resident alternate. 
  Dir single_doc_dir;           // Directory for the resident alternate
  Dir first_dir;                // Dir for the vector. If empty, a new dir is 
  // inserted, otherwise this dir is overwritten
  inku16 num_writers;           // num of current writers
  inku16 max_writers;           // max number of simultaneous writers allowed
  bool dont_update_directory;   // if set, the first_dir is not updated.
  bool move_resident_alt;       // if set, single_doc_dir is inserted.
  volatile bool reading_vec;    // somebody is currently reading the vector
  volatile bool writing_vec;    // somebody is currently writing the vector

    Link<OpenDirEntry> link;

  int wait(CacheVC * c, int msec);

  bool has_multiple_writers()
  {
    return num_writers > 1;
  }
};

struct OpenDir:Continuation
{
  Queue<CacheVC> delayed_readers;
  DLL<OpenDirEntry> bucket[OPEN_DIR_BUCKETS];

  int open_write(CacheVC * c, int allow_if_writers, int max_writers);
  int close_write(CacheVC * c);
  OpenDirEntry *open_read(INK_MD5 * key);
  int signal_readers(int event, Event * e);

    OpenDir();
};

struct CacheSync:Continuation
{
  int part;
  char *buf;
  int buflen;
  int writepos;
  AIOCallbackInternal io;
  Event *trigger;
  int mainEvent(int event, Event * e);
  void aio_write(int fd, char *b, int n, ink_off_t o);

    CacheSync():Continuation(new_ProxyMutex()), part(0), buf(0), buflen(0), writepos(0), trigger(0)
  {
    SET_HANDLER(&CacheSync::mainEvent);
  }
};

// Global Functions

void part_init_dir(Part * d);
int dir_token_probe(CacheKey *, Part *, Dir *);
int dir_probe(CacheKey *, Part *, Dir *, Dir **);
int dir_insert(CacheKey * key, Part * d, Dir * to_part);
int dir_overwrite(CacheKey * key, Part * d, Dir * to_part, Dir * overwrite, bool must_overwrite = true);
int dir_delete(CacheKey * key, Part * d, Dir * del);
int dir_lookaside_probe(CacheKey * key, Part * d, Dir * result, EvacuationBlock ** eblock);
int dir_lookaside_insert(EvacuationBlock * b, Part * d, Dir * to);
int dir_lookaside_fixup(CacheKey * key, Part * d);
void dir_lookaside_cleanup(Part * d);
void dir_lookaside_remove(CacheKey * key, Part * d);
void dir_free_entry(Dir * e, int s, Part * d);
void dir_sync_init();
void check_dir(Part * d);
void dir_clean_part(Part * d);
void dir_clear_range(int start, int end, Part * d);
int dir_segment_accounted(int s, Part * d, int offby = 0,
                          int *free = 0, int *used = 0,
                          int *empty = 0, int *valid = 0, int *agg_valid = 0, int *avg_size = 0);
void dir_compute_stats();

inku64 dir_entries_used(Part * d);

extern Dir empty_dir;

// inline Funtions

inline bool
dir_compare_tag(Dir * e, CacheKey * key)
{
  return (dir_tag(e) == DIR_MASK_TAG(key->word(1)));
}

inline Dir *
dir_from_offset(int i, Dir * seg)
{
#if DIR_DEPTH < 5
  if (!i)
    return 0;
  return &seg[i];
#else
  i = i + ((i - 1) / (DIR_DEPTH - 1));
  return &seg[i];
#endif
}
inline Dir *
next_dir(Dir * d, Dir * seg)
{
  int i = dir_next(d);
  return dir_from_offset(i, seg);
}
inline int
dir_to_offset(Dir * d, Dir * seg)
{
#if DIR_DEPTH < 5
  return d - seg;
#else
  int i = d - seg;
  i = i - (i / DIR_DEPTH);
  return i;
#endif
}
inline Dir *
dir_bucket(int b, Dir * seg)
{
  return &seg[b * DIR_DEPTH];
}
inline Dir *
dir_bucket_row(Dir * b, int i)
{
  return &b[i];
}

extern void sync_cache_dir_on_shutdown(void);

#endif /* _P_CACHE_DIR_H__ */
