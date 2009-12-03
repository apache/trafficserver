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


#ifndef _P_CACHE_PART_H__
#define _P_CACHE_PART_H__

// Definitions
#define AIO_NOT_IN_PROGRESS             0
#define AIO_AGG_WRITE_IN_PROGRESS       -1
#define SHM_MAGIC                       "TrafficServerCache"
#define SHM_SIZE                        128
#define AUTO_SIZE_RAM_CACHE             -1      // 1-1 with directory size

#define INK_BLOCK_SIZE			512
#define INK_BLOCK_SHIFT                 9
#define B8K_SIZE			8192
#define B8K_SHIFT                       13
#define ROUND_TO_16(_x)                 (((_x)+(16-1))&~(16-1))
#define ROUND_TO_BLOCK(_x)	(((_x)+(INK_BLOCK_SIZE-1))&~(INK_BLOCK_SIZE-1))
#define ROUND_TO_8K(_x) 		(((_x)+(B8K_SIZE-1))&~(B8K_SIZE-1))
#define ROUND_TO_PAGE(_x) 		(((_x)+(STORE_BLOCK_SIZE-1))&\
                                          ~(STORE_BLOCK_SIZE-1))
#define START_BLOCKS                    32      // 8k
#define START_POS			((ink_off_t)START_BLOCKS * INK_BLOCK_SIZE)
#define AGG_BLOCKS                      1024
#define AGG_HEADER_SIZE                 INK_BLOCK_SIZE
#define AGG_SIZE                        (AGG_BLOCKS * INK_BLOCK_SIZE)   // 512k
#define EVAC_SIZE                       (2 * AGG_SIZE)  // 1MB
#define MAX_PART_SIZE                   ((ink_off_t)8 * 1024 * 1024 * 1024)
#define STORE_BLOCKS_PER_DISK_BLOCK     (STORE_BLOCK_SIZE / INK_BLOCK_SIZE)
#define MAX_PART_BLOCKS                 (MAX_PART_SIZE / INK_BLOCK_SIZE)
#define TARGET_FRAG_SIZE                (32768 - sizeofDoc)
#define SHRINK_TARGET_FRAG_SIZE         (32768 + 8192)
#define MAX_FRAG_SIZE                   ((256 * 1024) - sizeofDoc)
#define AGG_TODO_SIZE_MAX               (5242880)       // 5 MB
#define LEAVE_FREE                      32768
#define PIN_SCAN_EVERY                  16      // scan every 1/16 of disk

#define PART_HASH_TABLE_SIZE            32707
#define PART_HASH_EMPTY                 0xFFFF

#define LOOKASIDE_SIZE                  256

// Part

#define PART_MAGIC			0xF1D0F00D
#define EVACUATION_BUCKET_SIZE          (2 * 1024 * 1024)

#define dir_offset_evac_bucket(_o) \
  (_o / (EVACUATION_BUCKET_SIZE / INK_BLOCK_SIZE))
#define dir_evac_bucket(_e) dir_offset_evac_bucket(dir_offset(_e))
#define offset_evac_bucket(_d, _o) \
  dir_offset_evac_bucket((offset_to_part_offset(_d, _o)

// Documents

#define DOC_MAGIC                       ((inku32)0x5F129B13)
#define DOC_CORRUPT                     ((inku32)0xDEADBABE)
#define DOC_NO_CHECKSUM                 ((inku32)0xA0B0C0D0)

#define sizeofDoc ((int)(long)&((Doc*)0)->hdr[0])


enum
{ CACHE_METADATA_INSERT, CACHE_METADATA_UPDATE,
  CACHE_METADATA_RESERVED, CACHE_METADATA_RESERVED2
};

struct Cache;
struct CacheDisk;
struct PartInitInfo;
struct DiskPart;
struct CachePart;

struct PartHeaderFooter
{
  unsigned int magic;
  VersionNumber version;
  ink_off_t write_pos;
  ink_off_t last_write_pos;
  inku32 generation;            // token generation (vary), this cannot be 0
  ink_off_t agg_pos;
  unsigned int phase;
  unsigned int cycle;
  unsigned int sync_serial;
  unsigned int write_serial;
  time_t create_time;
  unsigned int dirty;
  unsigned short freelist[DIR_SEGMENTS];
};

// Key and Earliest key for each fragment that needs to be evacuated
struct EvacuationKey
{
  SLink<EvacuationKey> link;
  INK_MD5 key;
  INK_MD5 earliest_key;
};

struct EvacuationBlock
{
  union
  {
    unsigned int init;
    struct
    {
      unsigned int readers:16;  // normal readers
      unsigned int done:1;      // has been evacuated
      unsigned int pinned:1;    // check pinning timeout
      unsigned int evacuate_head:1;     // check pinning timeout
      unsigned int unused:13;
    } f;
  };
  Dir dir;
  Dir new_dir;
  // we need to have a list of evacuationkeys because of collision.
  EvacuationKey evac_frags;
  CacheVC *earliest_evacuator;
    Link<EvacuationBlock> link;
};

struct MetaData
{
  union
  {
    struct
    {
      unsigned int op:2;
      unsigned int unused:30;
      unsigned int doc_serial;
      ink_off_t write_pos;
    } hdr;
    struct
    {
      unsigned int op:2;
      unsigned int hosthash:14;
      unsigned int segment:4;
      unsigned int bucket:12;
      unsigned int pin_time;
      Dir dir;
    } insert;
    struct
    {
      unsigned int op:2;
      unsigned int hosthash:14;
      unsigned int segment:4;
      unsigned int bucket:12;
      unsigned int new_offset;
      Dir dir;
    } update;
  };
};

struct PartCallback:public Continuation
{

  Queue<CacheVC> write_done;
  Event *trigger;

  int aggWriteDone(int event, Event * e);

    PartCallback(ProxyMutex * m):Continuation(m), trigger(0)
  {
    SET_HANDLER(&PartCallback::aggWriteDone);
  }
};

struct Part:public Continuation
{
  char *path;
  char *hash_id;
  INK_MD5 hash_id_md5;
  int fd;

  char *raw_dir;
  Dir *dir;
  PartHeaderFooter *header;
  PartHeaderFooter *footer;
  int buckets;
  Dir *segment[DIR_SEGMENTS];
  MetaData *metadata;
  ink_off_t recover_pos;
  ink_off_t prev_recover_pos;
  ink_off_t scan_pos;
  ink_off_t metadata_pos;
  ink_off_t skip;               // start of headers
  ink_off_t start;              // start of data
  ink_off_t len;
  int data_blocks;
  int hit_evacuate_window;

  AIOCallbackInternal io;

    Queue<CacheVC> agg;
    Queue<CacheVC> stat_cache_vcs;
  PartCallback *callback_cont;
  char *agg_buffer;
  int agg_todo_size;
  int agg_buf_pos;

  Event *trigger;

  OpenDir open_dir;
  RamCache ram_cache;
  int evacuate_size;
    DLL<EvacuationBlock> *evacuate;
    DLL<EvacuationBlock> lookaside[LOOKASIDE_SIZE];
  CacheVC *doc_evacuator;

  PartInitInfo *init_info;

  CacheDisk *disk;
  Cache *cache;
  CachePart *cache_part;
  inku32 last_sync_serial;
  inku32 last_write_serial;
  bool recover_wrapped;
  int dir_sync_waiting;
  int dir_sync_in_progress;
  RamCacheEntry first_fragment;


  void cancel_trigger();

  int open_write(CacheVC * cont, int allow_if_writers, int max_writers);
  int open_write_lock(CacheVC * cont, int allow_if_writers, int max_writers);
  int close_write(CacheVC * cont);
  int close_write_lock(CacheVC * cont);
  int begin_read(CacheVC * cont);
  int begin_read_lock(CacheVC * cont);
  // unused read-write interlock code
  // currently http handles a write-lock failure by retrying the read
  OpenDirEntry *open_read(INK_MD5 * key);
  OpenDirEntry *open_read_lock(INK_MD5 * key, EThread * t);
  int close_read(CacheVC * cont);
  int close_read_lock(CacheVC * cont);

  int clear_dir();

  int init(char *s, ink_off_t blocks, ink_off_t dir_skip, bool clear);

  int handle_dir_clear(int event, void *data);
  int handle_dir_read(int event, void *data);
  int handle_recover_from_data(int event, void *data);
  int handle_recover_write_dir(int event, void *data);
  int handle_header_read(int event, void *data);

  int dir_init_done(int event, void *data);

  int dir_check(bool fix);
  int db_check(bool fix);

  int is_io_in_progress()
  {
    return io.aiocb.aio_fildes != AIO_NOT_IN_PROGRESS;
  }
  int increment_generation()
  {
    // this is stored in the offset field of the directory (!=0)
    ink_debug_assert(mutex->thread_holding == this_ethread());
    header->generation++;
    if (!header->generation)
      header->generation++;
    return header->generation;
  }
  void set_io_not_in_progress()
  {
    io.aiocb.aio_fildes = AIO_NOT_IN_PROGRESS;
  }

  int aggWriteDone(int event, Event * e);
  int aggWrite(int event, Event * e);
  void agg_wrap();

  int evacuateWrite(CacheVC * evacuator, int event, Event * e);
  int evacuateDocReadDone(int event, Event * e);
  int evacuateDoc(int event, Event * e);

  int evac_range(ink_off_t start, ink_off_t end, int evac_phase);
  void periodic_scan();
  void scan_for_pinned_documents();
  void evacuate_cleanup_blocks(int i);
  void evacuate_cleanup();
  EvacuationBlock *force_evacuate_head(Dir * dir, int pinned);
  int within_hit_evacuate_window(Dir * dir);

Part():Continuation(new_ProxyMutex()), path(NULL), fd(-1),
    dir(0), buckets(0), recover_pos(0), prev_recover_pos(0), scan_pos(0), metadata_pos(0), skip(0), start(0),
    len(0), data_blocks(0), hit_evacuate_window(0), agg_todo_size(0), agg_buf_pos(0), trigger(0),
    evacuate_size(0), disk(NULL), last_sync_serial(0), last_write_serial(0), recover_wrapped(false),
    dir_sync_waiting(0), dir_sync_in_progress(0) {
    open_dir.mutex = mutex;
    callback_cont = NEW(new PartCallback(mutex));
#if defined(_WIN32)
    agg_buffer = (char *) malloc(AGG_SIZE);
#else
    agg_buffer = (char *) ink_memalign(sysconf(_SC_PAGESIZE), AGG_SIZE);
#endif
    memset(agg_buffer, 0, AGG_SIZE);
    SET_HANDLER(&Part::aggWrite);
  }

  ~Part() {
    delete callback_cont;
    ink_memalign_free(agg_buffer);
  }
};

struct AIO_Callback_handler:public Continuation
{

  int handle_disk_failure(int event, void *data);

    AIO_Callback_handler():Continuation(new_ProxyMutex())
  {
    SET_HANDLER(&AIO_Callback_handler::handle_disk_failure);
  }

};

struct CachePart
{
  int part_number;
  int scheme;
  int size;
  int num_parts;
  Part **parts;
  DiskPart **disk_parts;
    Link<CachePart> link;
  // per partition stats
  RecRawStatBlock *part_rsb;

    CachePart():part_number(-1), scheme(0), size(0), num_parts(0), parts(NULL), disk_parts(0), part_rsb(0)
  {
  }


};

// Note : the hdr field needs to be 8 byte aligned. 
struct Doc
{
  inku32 magic;                 // DOC_MAGIC
  ink32 len;                    // length of this segment
  ink32 hlen;                   // header length
  ink32 total_len;              // total length of document
  INK_MD5 first_key;            // first key in document (http: vector)
  INK_MD5 key;
  inku32 sync_serial;
  inku32 write_serial;
  inku32 pinned;                // pinned until
  inku32 checksum;
  char hdr[1];


  int data_len();
  int single_segment();
  int no_data_in_segment();
  char *data();
};

// Global Data

extern Part **gpart;
extern volatile int gnpart;
extern ClassAllocator<OpenDirEntry> openDirEntryAllocator;
extern ClassAllocator<EvacuationBlock> evacuationBlockAllocator;
extern ClassAllocator<EvacuationKey> evacuationKeyAllocator;
extern unsigned short *part_hash_table;

// inline Functions

inline int
part_dirlen(Part * d)
{
  return ROUND_TO_BLOCK(d->buckets *
                        DIR_DEPTH * DIR_SEGMENTS * SIZEOF_DIR) + 2 * ROUND_TO_BLOCK(sizeof(PartHeaderFooter));
}
inline int
part_direntries(Part * d)
{
  return d->buckets * DIR_DEPTH * DIR_SEGMENTS;
}
inline int
part_metalen(Part * e)
{
  (void) e;
  return 0;
}
inline int
part_out_of_phase_valid(Part * d, Dir * e)
{
  return (dir_offset(e) - 1 >= ((d->header->agg_pos - d->start) / INK_BLOCK_SIZE));
}
inline int
part_out_of_phase_agg_valid(Part * d, Dir * e)
{
  return (dir_offset(e) - 1 >= ((d->header->agg_pos - d->start + AGG_SIZE) / INK_BLOCK_SIZE));
}
inline int
part_out_of_phase_write_valid(Part * d, Dir * e)
{
  return (dir_offset(e) - 1 >= ((d->header->write_pos - d->start) / INK_BLOCK_SIZE));
}
inline int
part_in_phase_valid(Part * d, Dir * e)
{
  return (dir_offset(e) - 1 < ((d->header->write_pos + d->agg_buf_pos - d->start) / INK_BLOCK_SIZE));
}
inline ink_off_t
part_offset(Part * d, Dir * e)
{
  return d->start + (ink_off_t) dir_offset(e) * INK_BLOCK_SIZE - INK_BLOCK_SIZE;
}
inline int
offset_to_part_offset(Part * d, ink_off_t pos)
{
  return (int) ((pos - d->start + INK_BLOCK_SIZE) / INK_BLOCK_SIZE);
}
inline ink_off_t
part_offset_to_offset(Part * d, ink_off_t pos)
{
  return d->start + pos * INK_BLOCK_SIZE - INK_BLOCK_SIZE;
}
inline Dir *
part_dir_segment(Part * d, int s)
{
  return (Dir *) (((char *) d->dir) + (s * d->buckets) * DIR_DEPTH * SIZEOF_DIR);
}
inline int
part_in_phase_agg_buf_valid(Part * d, Dir * e)
{
  return (part_offset(d, e) >= d->header->write_pos && part_offset(d, e) < (d->header->write_pos + d->agg_buf_pos));
}

inline int
Doc::data_len()
{
  return len - sizeofDoc - hlen;
}
inline int
Doc::single_segment()
{
  return (total_len && (data_len() == total_len));
}
inline char *
Doc::data()
{
  return ((char *) this) + sizeofDoc + hlen;
}

int part_dir_clear(Part * d);
int part_init(Part * d, char *s, ink_off_t blocks, ink_off_t skip, bool clear);

// inline Functions

inline EvacuationBlock *
evacuation_block_exists(Dir * dir, Part * p)
{
  EvacuationBlock *b = p->evacuate[dir_evac_bucket(dir)].head;
  for (; b; b = b->link.next)
    if (dir_offset(&b->dir) == dir_offset(dir))
      return b;
  return 0;
}

inline void
Part::cancel_trigger()
{
  if (trigger) {
    trigger->cancel_action();
    trigger = NULL;
  }
}

inline EvacuationBlock *
new_EvacuationBlock(EThread * t)
{
  EvacuationBlock *b = THREAD_ALLOC(evacuationBlockAllocator, t);
  b->init = 0;
  b->earliest_evacuator = 0;
  b->evac_frags.link.next = 0;
  return b;
}

inline void
free_EvacuationBlock(EvacuationBlock * b, EThread * t)
{
  EvacuationKey *e = b->evac_frags.link.next;
  while (e) {
    EvacuationKey *n = e->link.next;
    evacuationKeyAllocator.free(e);
    e = n;
  }
  THREAD_FREE(b, evacuationBlockAllocator, t);
}

inline OpenDirEntry *
Part::open_read(INK_MD5 * key)
{
  return open_dir.open_read(key);
}

inline int
Part::within_hit_evacuate_window(Dir * xdir)
{
  ink_off_t oft = dir_offset(xdir) - 1;
  ink_off_t write_off = (header->write_pos + AGG_SIZE - start) / INK_BLOCK_SIZE;
  ink_off_t delta = oft - write_off;
  if (delta >= 0)
    return delta < hit_evacuate_window;
  else
    return -delta > (data_blocks - hit_evacuate_window) && -delta < data_blocks;
}

#endif /* _P_CACHE_PART_H__ */
