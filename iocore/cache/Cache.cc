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




#include "P_Cache.h"
#include "P_CacheTest.h"

// Cache Inspector and State Pages
#ifdef NON_MODULAR
#include "StatPages.h"
#endif

#ifdef HTTP_CACHE
#include "HttpTransactCache.h"
#endif

#include "InkAPIInternal.h"
#include <HttpSM.h>
#include <HttpCacheSM.h>

// Compilation Options

#define USELESS_REENABLES       // allow them for now
// #define VERIFY_JTEST_DATA

#define DOCACHE_CLEAR_DYN_STAT(x) \
do { \
	RecSetRawStatSum(rsb, x, 0); \
	RecSetRawStatCount(rsb, x, 0); \
} while (0);

// Configuration

ink64 cache_config_ram_cache_size = AUTO_SIZE_RAM_CACHE;
int cache_config_http_max_alts = 3;
int cache_config_dir_sync_frequency = 60;
int cache_config_permit_pinning = 0;
int cache_config_vary_on_user_agent = 0;
int cache_config_select_alternate = 1;
int cache_config_max_doc_size = 0;
int cache_config_min_average_object_size = ESTIMATED_OBJECT_SIZE;
ink64 cache_config_ram_cache_cutoff = 1048576;  // 1 MB
ink64 cache_config_ram_cache_mixt_cutoff = 1048576;     // 1 MB
int cache_config_max_disk_errors = 5;
int cache_config_agg_write_backlog = 5242880;
#ifdef HIT_EVACUATE
int cache_config_hit_evacuate_percent = 10;
int cache_config_hit_evacuate_size_limit = 0;
#endif
int cache_config_enable_checksum = 0;
int cache_config_alt_rewrite_max_size = 4096;
int cache_config_read_while_writer = 0;
char cache_system_config_directory[PATH_NAME_MAX + 1];
// Globals
RecRawStatBlock *cache_rsb = NULL;
Cache *theStreamCache = 0;
Cache *theCache = 0;
CacheDisk **gdisks = NULL;
int gndisks = 0;
static volatile int initialize_disk = 0;
Cache *caches[1 << NumCacheFragTypes] = { 0 };
CacheSync *cacheDirSync = 0;
Store theCacheStore;
volatile int CacheProcessor::initialized = CACHE_INITIALIZING;
volatile inku32 CacheProcessor::cache_ready = 0;
volatile int CacheProcessor::start_done = 0;
int CacheProcessor::clear = 0;
int CacheProcessor::fix = 0;
int CacheProcessor::start_internal_flags = 0;
int CacheProcessor::auto_clear_flag = 0;
CacheProcessor cacheProcessor;
Part **gpart = NULL;
volatile int gnpart = 0;
ClassAllocator<CacheVC> cacheVConnectionAllocator("cacheVConnection");
ClassAllocator<NewCacheVC> newCacheVConnectionAllocator("newCacheVConnection");
ClassAllocator<EvacuationBlock> evacuationBlockAllocator("evacuationBlock");
ClassAllocator<CacheRemoveCont> cacheRemoveContAllocator("cacheRemoveCont");
ClassAllocator<EvacuationKey> evacuationKeyAllocator("evacuationKey");
int CacheVC::size_to_init = -1;
CacheKey zero_key(0, 0);

struct PartInitInfo
{
  ink_off_t recover_pos;
  AIOCallbackInternal part_aio[4];
  char *part_h_f;

  PartInitInfo()
  {
    recover_pos = 0;
    if ((part_h_f = (char *) valloc(4 * INK_BLOCK_SIZE)) != NULL)
      memset(part_h_f, 0, 4 * INK_BLOCK_SIZE);
  }
  ~PartInitInfo() 
  {
    for (int i = 0; i < 4; i++) {
      part_aio[i].action = NULL;
      part_aio[i].mutex.clear();
    }
    free(part_h_f);
  }
};

void cplist_init();
static void cplist_update();
int cplist_reconfigure();
static int create_partition(int partition_number, int size_in_blocks, int scheme, CachePart *cp);
static void rebuild_host_table(Cache *cache);
void register_cache_stats(RecRawStatBlock *rsb, const char *prefix);

Queue<CachePart> cp_list;
int cp_list_len = 0;
ConfigPartitions config_partitions;

ink64
cache_bytes_used(void)
{
  inku64 used = 0;
  for (int i = 0; i < gnpart; i++) {
    if (!DISK_BAD(gpart[i]->disk)) {
      if (!gpart[i]->header->cycle)
        used += gpart[i]->header->write_pos - gpart[i]->start;
      else
        used += gpart[i]->len - part_dirlen(gpart[i]) - EVACUATION_SIZE;
    }
  }
  return used;
}

ink64
cache_bytes_total(void)
{
  ink64 total = 0;
  for (int i = 0; i < gnpart; i++)
    total += gpart[i]->len - part_dirlen(gpart[i]) - EVACUATION_SIZE;

  return total;
}

int
cache_stats_bytes_used_cb(const char *name,
                          RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id, void *cookie)
{
  if (cacheProcessor.initialized == CACHE_INITIALIZED) {
    RecSetGlobalRawStatSum(rsb, id, cache_bytes_used());
  }
  return 1;
}

static int
update_cache_config(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  (void) name;
  (void) data_type;
  (void) cookie;
  int new_value = data.rec_int;
  if (new_value) {
    float http_bg_fill;
    IOCORE_ReadConfigFloat(http_bg_fill, "proxy.config.http.background_fill_completed_threshold");
    if (http_bg_fill > 0.0) {
      Note("to enable reading while writing a document, %s should be 0.0: read while writing disabled",
           "proxy.config.http.background_fill_completed_threshold");
      return 0;
    }
    if (cache_config_max_doc_size > 0) {
      Note("to enable reading while writing a document, %s should be 0: read while writing disabled",
           "proxy.config.cache.max_doc_size");
      return 0;
    }
  }
  cache_config_read_while_writer = new_value;
  return 0;
}

CacheVC::CacheVC():alternate_index(CACHE_ALT_INDEX_DEFAULT)
{
  size_to_init = sizeof(CacheVC) - (size_t) & ((CacheVC *) 0)->vio;
  memset((char *) &vio, 0, size_to_init);
  // the constructor does a memset() on the members that need to be initialized
  //coverity[uninit_member]
}

VIO *
CacheVC::do_io_read(Continuation *c, ink64 nbytes, MIOBuffer *abuf)
{
  ink_assert(vio.op == VIO::READ);
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone = 0;
  vio.nbytes = nbytes;
  vio.vc_server = this;
  ink_assert(c->mutex->thread_holding);
  if (!trigger && !recursive)
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  return &vio;
}

VIO *
CacheVC::do_io_pread(Continuation *c, ink64 nbytes, MIOBuffer *abuf, ink64 offset)
{
  ink_assert(vio.op == VIO::READ);
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone = offset;
  vio.nbytes = 0;
  vio.vc_server = this;
  seek_to = offset;
  ink_assert(c->mutex->thread_holding);
  if (!trigger && !recursive)
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  return &vio;
}

VIO *
CacheVC::do_io_write(Continuation *c, ink64 nbytes, IOBufferReader *abuf, bool owner)
{
  ink_assert(vio.op == VIO::WRITE);
  ink_assert(!owner);
  vio.buffer.reader_for(abuf);
  vio.set_continuation(c);
  vio.ndone = 0;
  vio.nbytes = nbytes;
  vio.vc_server = this;
  ink_assert(c->mutex->thread_holding);
  if (!trigger && !recursive)
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  return &vio;
}

void
CacheVC::do_io_close(int alerrno)
{
  ink_debug_assert(mutex->thread_holding == this_ethread());
  int previous_closed = closed;
  closed = (alerrno == -1) ? 1 : -1;    // Stupid default arguments
  DDebug("cache_close", "do_io_close %lX %d %d", (long) this, alerrno, closed);
  if (!previous_closed && !recursive)
    die();
}

void
CacheVC::reenable(VIO *avio)
{
  DDebug("cache_reenable", "reenable %lX", (long) this);
  (void) avio;
  ink_assert(avio->mutex->thread_holding);
  if (!trigger) {
#ifndef USELESS_REENABLES
    if (vio.op == VIO::READ) {
      if (vio.buffer.mbuf->max_read_avail() > vio.buffer.writer()->water_mark)
        ink_assert(!"useless reenable of cache read");
    } else if (!vio.buffer.reader()->read_avail())
      ink_assert(!"useless reenable of cache write");
#endif
    trigger = avio->mutex->thread_holding->schedule_imm_local(this);
  }
}

void
CacheVC::reenable_re(VIO *avio)
{
  DDebug("cache_reenable", "reenable_re %lX", (long) this);
  (void) avio;
  ink_assert(avio->mutex->thread_holding);
  if (!trigger) {
    if (!is_io_in_progress() && !recursive) {
      handleEvent(EVENT_NONE, (void *) 0);
    } else
      trigger = avio->mutex->thread_holding->schedule_imm_local(this);
  }
}

bool CacheVC::get_data(int i, void *data)
{
  switch (i) {
  case CACHE_DATA_SIZE:
    *((int *) data) = doc_len;
    return true;
#ifdef HTTP_CACHE
  case CACHE_DATA_HTTP_INFO:
    *((CacheHTTPInfo **) data) = &alternate;
    return true;
#endif
  case CACHE_DATA_RAM_CACHE_HIT_FLAG:
    *((int *) data) = !f.not_from_ram_cache;
    return true;
  default:
    break;
  }
  return false;
}

int
CacheVC::get_object_size()
{
  return ((CacheVC *) this)->doc_len;
}

bool CacheVC::set_data(int i, void *data)
{
  (void) i;
  (void) data;
  ink_debug_assert(!"CacheVC::set_data should not be called!");
  return true;
}

#ifdef HTTP_CACHE
void
CacheVC::get_http_info(CacheHTTPInfo ** ainfo)
{
  *ainfo = &((CacheVC *) this)->alternate;
}

// set_http_info must be called before do_io_write
// cluster vc does an optimization where it calls do_io_write() before
// calling set_http_info(), but it guarantees that the info will
// be set before transferring any bytes
void
CacheVC::set_http_info(CacheHTTPInfo *ainfo)
{
  ink_assert(!total_len);
  if (f.update) {
    ainfo->object_key_set(update_key);
    ainfo->object_size_set(update_len);
  } else {
    ainfo->object_key_set(earliest_key);
    // don't know the total len yet
  }
  alternate.copy_shallow(ainfo);
  ainfo->clear();
}
#endif

bool CacheVC::set_pin_in_cache(time_t time_pin)
{
  if (total_len) {
    ink_assert(!"should Pin the document before writing");
    return false;
  }
  if (vio.op != VIO::WRITE) {
    ink_assert(!"Pinning only allowed while writing objects to the cache");
    return false;
  }
  pin_in_cache = time_pin;
  return true;
}

bool CacheVC::set_disk_io_priority(int priority)
{

  ink_assert(priority >= AIO_LOWEST_PRIORITY);
  io.aiocb.aio_reqprio = priority;
  return true;
}

time_t CacheVC::get_pin_in_cache()
{
  return pin_in_cache;
}

int
CacheVC::get_disk_io_priority()
{
  return io.aiocb.aio_reqprio;
}

int
Part::begin_read(CacheVC *cont)
{
  ink_debug_assert(cont->mutex->thread_holding == this_ethread());
  ink_debug_assert(mutex->thread_holding == this_ethread());
#ifdef CACHE_STAT_PAGES
  ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
  stat_cache_vcs.enqueue(cont, cont->stat_link);
#endif
  // no need for evacuation as the entire document is already in memory
  if (cont->f.single_fragment)
    return 0;
  int i = dir_evac_bucket(&cont->earliest_dir);
  EvacuationBlock *b;
  for (b = evacuate[i].head; b; b = b->link.next) {
    if (dir_offset(&b->dir) != dir_offset(&cont->earliest_dir))
      continue;
    if (b->readers)
      b->readers = b->readers + 1;
    return 0;
  }
  // we don't actually need to preserve this block as it is already in
  // memory, but this is easier, and evacuations are rare
  EThread *t = cont->mutex->thread_holding;
  b = new_EvacuationBlock(t);
  b->readers = 1;
  b->dir = cont->earliest_dir;
  b->evac_frags.key = cont->earliest_key;
  evacuate[i].push(b);
  return 1;
}

int
Part::close_read(CacheVC *cont)
{
  EThread *t = cont->mutex->thread_holding;
  ink_debug_assert(t == this_ethread());
  ink_debug_assert(t == mutex->thread_holding);
  if (dir_is_empty(&cont->earliest_dir))
    return 1;
  int i = dir_evac_bucket(&cont->earliest_dir);
  EvacuationBlock *b;
  for (b = evacuate[i].head; b;) {
    EvacuationBlock *next = b->link.next;
    if (dir_offset(&b->dir) != dir_offset(&cont->earliest_dir)) {
      b = next;
      continue;
    }
    if (b->readers && !--b->readers) {
      evacuate[i].remove(b);
      free_EvacuationBlock(b, t);
      break;
    }
    b = next;
  }
#ifdef CACHE_STAT_PAGES
  stat_cache_vcs.remove(cont, cont->stat_link);
  ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
#endif
  return 1;
}

// Cache Processor

int
CacheProcessor::start(int)
{
  return start_internal(0);
}

int
CacheProcessor::start_internal(int flags)
{
  verify_cache_api();

  start_internal_flags = flags;
  clear = !!(flags & PROCESSOR_RECONFIGURE) || auto_clear_flag;
  fix = !!(flags & PROCESSOR_FIX);
  int i, p;
  start_done = 0;
  int diskok = 1;

  /* read the config file and create the data structures corresponding
     to the file */
  gndisks = theCacheStore.n_disks;
  gdisks = (CacheDisk **) xmalloc(gndisks * sizeof(CacheDisk *));

  gndisks = 0;
  ink_aio_set_callback(new AIO_Callback_handler());
  Span *sd;
  config_partitions.read_config_file();
  for (i = 0; i < theCacheStore.n_disks; i++) {
    sd = theCacheStore.disk[i];
    char path[PATH_MAX];
    int opts = O_RDWR;
    ink_strncpy(path, sd->pathname, sizeof(path));
    if (!sd->file_pathname) {
#if !defined(_WIN32)
      if (config_partitions.num_http_partitions && config_partitions.num_stream_partitions) {
        Warning("It is suggested that you use raw disks if streaming and http are in the same cache");
      }
#endif
      strncat(path, DIR_SEP "cache.db", (sizeof(path) - strlen(path) - 1));
      opts |= O_CREAT;
    }
    opts |= _O_ATTRIB_OVERLAPPED;
#ifdef O_DIRECT
    opts |= O_DIRECT;
#endif
#ifdef O_DSYNC
    opts |= O_DSYNC;
#endif

    int fd = ink_open(path, opts, 0644);
    int blocks = sd->blocks;
    ink_off_t offset = sd->offset;
    p = 0;
    if (fd > 0) {
#if defined (_WIN32)
      aio_completion_port.register_handle((void *) fd, 0);
#endif
      if (!sd->file_pathname) {
        if (ink_ftruncate64(fd, ((inku64) blocks) * STORE_BLOCK_SIZE) < 0) {
          Warning("unable to truncate cache file '%s' to %d blocks", path, blocks);
          diskok = 0;
#if defined(_WIN32)
          /* We can do a specific check for FAT32 systems on NT, 
           * to print a specific warning */
          if ((((inku64) blocks) * STORE_BLOCK_SIZE) > (1 << 32)) {
            Warning("If you are using a FAT32 file system, please ensure that cachesize"
                    "specified in storage.config, does not exceed 4GB!. ");
          }
#endif
        }
      }
      if (diskok) {
        gdisks[gndisks] = NEW(new CacheDisk());
        Debug("cache_hosting", "Disk: %d, blocks: %d", gndisks, blocks);
        gdisks[gndisks]->open(path, blocks, offset, fd, clear);
        gndisks++;
      }
    } else
      Warning("cache unable to open '%s': %s", path, strerror(errno));
  }

  if (gndisks == 0) {
    Warning("unable to open cache disk(s): Cache Disabled\n");
    return -1;
  }
  start_done = 1;

  return 0;
}

void
CacheProcessor::diskInitialized()
{
  ink_atomic_increment(&initialize_disk, 1);
  int bad_disks = 0;
  int res = 0;
  if (initialize_disk == gndisks) {

    int i;
    for (i = 0; i < gndisks; i++) {
      if (DISK_BAD(gdisks[i]))
        bad_disks++;
    }

    if (bad_disks != 0) {
      // create a new array 
      CacheDisk **p_good_disks;
      if ((gndisks - bad_disks) > 0)
        p_good_disks = (CacheDisk **) xmalloc((gndisks - bad_disks) * sizeof(CacheDisk *));
      else
        p_good_disks = 0;

      int insert_at = 0;
      for (i = 0; i < gndisks; i++) {
        if (DISK_BAD(gdisks[i])) {
          delete gdisks[i];
          continue;
        }
        if (p_good_disks != NULL) {
          p_good_disks[insert_at++] = gdisks[i];
        }
      }
      xfree(gdisks);
      gdisks = p_good_disks;
      gndisks = gndisks - bad_disks;
    }

    /* create the cachepart list only if num partitions are greater 
       than 0. */
    if (config_partitions.num_partitions == 0) {
      res = cplist_reconfigure();
      /* if no partitions, default to just an http cache */
    } else {
      // else 
      /* create the cachepart list. */
      cplist_init();
      /* now change the cachepart list based on the config file */
      res = cplist_reconfigure();
    }

    if (res == -1) {
      /* problems initializing the partition.config. Punt */
      gnpart = 0;
      cacheInitialized();
      return;
    } else {
      CachePart *cp = cp_list.head;
      for (; cp; cp = cp->link.next) {
        cp->part_rsb = RecAllocateRawStatBlock((int) cache_stat_count);
        char part_stat_str_prefix[256];
        snprintf(part_stat_str_prefix, sizeof(part_stat_str_prefix), "proxy.process.cache.partition_%d",
                 cp->part_number);
        register_cache_stats(cp->part_rsb, part_stat_str_prefix);

      }
    }

    gpart = (Part **) xmalloc(gnpart * sizeof(Part *));
    memset(gpart, 0, gnpart * sizeof(Part *));
    gnpart = 0;
    for (i = 0; i < gndisks; i++) {
      CacheDisk *d = gdisks[i];
      if (is_debug_tag_set("cache_hosting")) {

        int j;
        Debug("cache_hosting", "Disk: %d: Part Blocks: %ld: Free space: %ld",
              i, d->header->num_diskpart_blks, d->free_space);
        for (j = 0; j < (int) d->header->num_partitions; j++) {
          Debug("cache_hosting", "\tPart: %d Size: %d", d->disk_parts[j]->part_number, d->disk_parts[j]->size);
        }
        for (j = 0; j < (int) d->header->num_diskpart_blks; j++) {
          Debug("cache_hosting", "\tBlock No: %d Size: %d Free: %d",
                d->header->part_info[j].number, d->header->part_info[j].len, d->header->part_info[j].free);
        }
      }
      d->sync();
    }
    if (config_partitions.num_partitions == 0) {
      theCache = NEW(new Cache());
      theCache->scheme = CACHE_HTTP_TYPE;
      theCache->open(clear, fix);
      return;
    }
    if (config_partitions.num_http_partitions != 0) {
      theCache = NEW(new Cache());
      theCache->scheme = CACHE_HTTP_TYPE;
      theCache->open(clear, fix);
    }

    if (config_partitions.num_stream_partitions != 0) {
      theStreamCache = NEW(new Cache());
      theStreamCache->scheme = CACHE_RTSP_TYPE;
      theStreamCache->open(clear, fix);
    }

  }
}

void
CacheProcessor::cacheInitialized()
{
  int i;

  if ((theCache && (theCache->ready == CACHE_INITIALIZING)) ||
      (theStreamCache && (theStreamCache->ready == CACHE_INITIALIZING)))
    return;
  int caches_ready = 0;
  int cache_init_ok = 0;
  /* allocate ram size in proportion to the disk space the
     partition accupies */
  ink64 total_size = 0;
  inku64 total_cache_bytes = 0;
  inku64 total_direntries = 0;
  inku64 used_direntries = 0;
  inku64 part_total_cache_bytes = 0;
  inku64 part_total_direntries = 0;
  inku64 part_used_direntries = 0;
  Part *part;

  ProxyMutex *mutex = this_ethread()->mutex;

  if (theCache) {
    total_size += theCache->cache_size;
    Debug("cache_init", "CacheProcessor::cacheInitialized - theCache, total_size = %lld = %lld",
          total_size, total_size / (1024 * 1024));
  }
  if (theStreamCache) {
    total_size += theStreamCache->cache_size;
    Debug("cache_init", "CacheProcessor::cacheInitialized - theStreamCache, total_size = %lld = %lld",
          total_size, total_size / (1024 * 1024));
  }

  if (theCache) {
    if (theCache->ready == CACHE_INIT_FAILED) {
      Debug("cache_init", "CacheProcessor::cacheInitialized - failed to initialize the cache for http: cache disabled");
      Warning("failed to initialize the cache for http: cache disabled\n");
    } else {
      caches_ready = caches_ready | CACHE_FRAG_TYPE_HTTP;
      caches_ready = caches_ready | CACHE_FRAG_TYPE_NONE;
      caches_ready = caches_ready | CACHE_FRAG_TYPE_NNTP;
      caches_ready = caches_ready | CACHE_FRAG_TYPE_FTP;
      caches[CACHE_FRAG_TYPE_HTTP] = theCache;
      caches[CACHE_FRAG_TYPE_NONE] = theCache;
      caches[CACHE_FRAG_TYPE_NNTP] = theCache;
      caches[CACHE_FRAG_TYPE_FTP] = theCache;
    }
  }
  if (theStreamCache) {
    if (theStreamCache->ready == CACHE_INIT_FAILED) {
      Debug("cache_init",
            "CacheProcessor::cacheInitialized - failed to initialize the cache for streaming: cache disabled");
      Warning("failed to initialize the cache for streaming: cache disabled\n");
    } else {
      caches_ready = caches_ready | CACHE_FRAG_TYPE_RTSP;
      caches[CACHE_FRAG_TYPE_RTSP] = theStreamCache;
    }
  }

  if (caches_ready) {
    Debug("cache_init", "CacheProcessor::cacheInitialized - caches_ready=0x%0lX, gnpart=%d",
          (unsigned long) caches_ready, gnpart);
    if (gnpart) {
      ink64 ram_cache_bytes = 0;
      ink32 ram_cache_object_size;
      if (cache_config_ram_cache_size == AUTO_SIZE_RAM_CACHE) {
        Debug("cache_init", "CacheProcessor::cacheInitialized - cache_config_ram_cache_size == AUTO_SIZE_RAM_CACHE");
        for (i = 0; i < gnpart; i++) {
          part = gpart[i];
          if (gpart[i]->cache == theCache) {
            ram_cache_object_size = ((cache_config_ram_cache_cutoff < cache_config_min_average_object_size) &&
                                     cache_config_ram_cache_cutoff) ?
              cache_config_ram_cache_cutoff : cache_config_min_average_object_size;
            gpart[i]->ram_cache.init(part_dirlen(gpart[i]), part_dirlen(gpart[i]) / ram_cache_object_size,
                                     cache_config_ram_cache_cutoff, gpart[i], gpart[i]->mutex);
          } else {
            ram_cache_object_size = ((cache_config_ram_cache_mixt_cutoff < cache_config_min_average_object_size) &&
                                     cache_config_ram_cache_mixt_cutoff) ?
              cache_config_ram_cache_mixt_cutoff : cache_config_min_average_object_size;
            gpart[i]->ram_cache.init(part_dirlen(gpart[i]), part_dirlen(gpart[i]) / ram_cache_object_size,
                                     cache_config_ram_cache_mixt_cutoff, gpart[i], gpart[i]->mutex);
          }
          ram_cache_bytes += part_dirlen(gpart[i]);
          Debug("cache_init", "CacheProcessor::cacheInitialized - ram_cache_bytes = %lld = %lldMb",
                ram_cache_bytes, ram_cache_bytes / (1024 * 1024));
          /*
             CACHE_PART_SUM_DYN_STAT(cache_ram_cache_bytes_total_stat, 
             (ink64)part_dirlen(gpart[i]));
           */
          RecSetGlobalRawStatSum(part->cache_part->part_rsb,
                                 cache_ram_cache_bytes_total_stat, (ink64) part_dirlen(gpart[i]));
          part_total_cache_bytes = gpart[i]->len - part_dirlen(gpart[i]);
          total_cache_bytes += part_total_cache_bytes;
          Debug("cache_init", "CacheProcessor::cacheInitialized - total_cache_bytes = %lld = %lldMb",
                total_cache_bytes, total_cache_bytes / (1024 * 1024));

          CACHE_PART_SUM_DYN_STAT(cache_bytes_total_stat, part_total_cache_bytes);


          part_total_direntries = gpart[i]->buckets * gpart[i]->segments * DIR_DEPTH;
          total_direntries += part_total_direntries;
          CACHE_PART_SUM_DYN_STAT(cache_direntries_total_stat, part_total_direntries);


          part_used_direntries = dir_entries_used(gpart[i]);
          CACHE_PART_SUM_DYN_STAT(cache_direntries_used_stat, part_used_direntries);
          used_direntries += part_used_direntries;
        }

      } else {
        Debug("cache_init", "CacheProcessor::cacheInitialized - %lld != AUTO_SIZE_RAM_CACHE",
              cache_config_ram_cache_size);
        ink64 http_ram_cache_size =
          (theCache) ? (ink64) (((double) theCache->cache_size / total_size) * cache_config_ram_cache_size) : 0;
        Debug("cache_init", "CacheProcessor::cacheInitialized - http_ram_cache_size = %lld = %lldMb",
              http_ram_cache_size, http_ram_cache_size / (1024 * 1024));
        ink64 stream_ram_cache_size = cache_config_ram_cache_size - http_ram_cache_size;
        Debug("cache_init", "CacheProcessor::cacheInitialized - stream_ram_cache_size = %lld = %lldMb",
              stream_ram_cache_size, stream_ram_cache_size / (1024 * 1024));

        // Dump some ram_cache size information in debug mode.
        Debug("ram_cache", "config: size = %lld, cutoff = %lld",
              cache_config_ram_cache_size, cache_config_ram_cache_cutoff);

        for (i = 0; i < gnpart; i++) {
          part = gpart[i];
          double factor;
          if (gpart[i]->cache == theCache) {
            factor = (double) (ink64) (gpart[i]->len >> STORE_BLOCK_SHIFT) / (ink64) theCache->cache_size;
            Debug("cache_init", "CacheProcessor::cacheInitialized - factor = %f", factor);
            gpart[i]->ram_cache.init((ink64) (http_ram_cache_size * factor),
                                     (ink64) ((http_ram_cache_size * factor) /
                                              cache_config_min_average_object_size),
                                     cache_config_ram_cache_cutoff, gpart[i], gpart[i]->mutex);

            ram_cache_bytes += (ink64) (http_ram_cache_size * factor);
            CACHE_PART_SUM_DYN_STAT(cache_ram_cache_bytes_total_stat, (ink64) (http_ram_cache_size * factor));
          } else {
            factor = (double) (ink64) (gpart[i]->len >> STORE_BLOCK_SHIFT) / (ink64) theStreamCache->cache_size;
            Debug("cache_init", "CacheProcessor::cacheInitialized - factor = %f", factor);
            gpart[i]->ram_cache.init((ink64) (stream_ram_cache_size * factor),
                                     (ink64) ((stream_ram_cache_size * factor) /
                                              cache_config_min_average_object_size),
                                     cache_config_ram_cache_mixt_cutoff, gpart[i], gpart[i]->mutex);

            ram_cache_bytes += (ink64) (stream_ram_cache_size * factor);
            CACHE_PART_SUM_DYN_STAT(cache_ram_cache_bytes_total_stat, (ink64) (stream_ram_cache_size * factor));


          }
          Debug("cache_init", "CacheProcessor::cacheInitialized[%d] - ram_cache_bytes = %lld = %lldMb",
                i, ram_cache_bytes, ram_cache_bytes / (1024 * 1024));

          part_total_cache_bytes = gpart[i]->len - part_dirlen(gpart[i]);
          total_cache_bytes += part_total_cache_bytes;
          CACHE_PART_SUM_DYN_STAT(cache_bytes_total_stat, part_total_cache_bytes);
          Debug("cache_init", "CacheProcessor::cacheInitialized - total_cache_bytes = %lld = %lldMb",
                total_cache_bytes, total_cache_bytes / (1024 * 1024));

          part_total_direntries = gpart[i]->buckets * gpart[i]->segments * DIR_DEPTH;
          total_direntries += part_total_direntries;
          CACHE_PART_SUM_DYN_STAT(cache_direntries_total_stat, part_total_direntries);


          part_used_direntries = dir_entries_used(gpart[i]);
          CACHE_PART_SUM_DYN_STAT(cache_direntries_used_stat, part_used_direntries);
          used_direntries += part_used_direntries;

        }
      }

      GLOBAL_CACHE_SET_DYN_STAT(cache_ram_cache_bytes_total_stat, ram_cache_bytes);
      GLOBAL_CACHE_SET_DYN_STAT(cache_bytes_total_stat, total_cache_bytes);
      GLOBAL_CACHE_SET_DYN_STAT(cache_direntries_total_stat, total_direntries);
      GLOBAL_CACHE_SET_DYN_STAT(cache_direntries_used_stat, used_direntries);
      dir_sync_init();
      cache_init_ok = 1;
    } else
      Warning("cache unable to open any parts, disabled");
  }
  if (cache_init_ok) {
    // Initialize virtual cache
    CacheProcessor::initialized = CACHE_INITIALIZED;
    CacheProcessor::cache_ready = caches_ready;
    Note("cache enabled");
#ifdef CLUSTER_CACHE
    if (!(start_internal_flags & PROCESSOR_RECONFIGURE)) {
      CacheContinuation::init();
      clusterProcessor.start();
    }
#endif
  } else {
    CacheProcessor::initialized = CACHE_INIT_FAILED;
    Note("cache disabled");
  }
}

void
CacheProcessor::stop()
{
}

int
CacheProcessor::dir_check(bool afix)
{
  for (int i = 0; i < gnpart; i++)
    gpart[i]->dir_check(afix);
  return 0;
}

int
CacheProcessor::db_check(bool afix)
{
  for (int i = 0; i < gnpart; i++)
    gpart[i]->db_check(afix);
  return 0;
}

int
Part::db_check(bool fix)
{
  (void) fix;
  char tt[256];
  printf("    Data for [%s]\n", hash_id);
  printf("        Blocks:          %d\n", (int) (len / INK_BLOCK_SIZE));
  printf("        Write Position:  %d\n", (int) ((header->write_pos - skip) / INK_BLOCK_SIZE));
  printf("        Phase:           %d\n", (int) !!header->phase);
  ink_ctime_r(&header->create_time, tt);
  tt[strlen(tt) - 1] = 0;
  printf("        Create Time:     %s\n", tt);
  printf("        Sync Serial:     %u\n", (int) header->sync_serial);
  printf("        Write Serial:    %u\n", (int) header->write_serial);
  printf("\n");

  return 0;
}

static void
part_init_data_internal(Part *d)
{
  d->buckets = ((d->len - (d->start - d->skip)) / cache_config_min_average_object_size) / DIR_DEPTH;
  d->segments = (d->buckets + (((1<<16)-1)/DIR_DEPTH)) / ((1<<16)/DIR_DEPTH);
  d->buckets = (d->buckets + d->segments - 1) / d->segments;
  d->start = d->skip + 2 *part_dirlen(d);
}

static void
part_init_data(Part *d) {
  // iteratively calculate start + buckets
  part_init_data_internal(d);
  part_init_data_internal(d);
  part_init_data_internal(d);
}

void
part_init_dir(Part *d)
{
  int b, s, l;

  for (s = 0; s < d->segments; s++) {
    d->header->freelist[s] = 0;
    Dir *seg = dir_segment(s, d);
    for (l = 1; l < DIR_DEPTH; l++) {
      for (b = 0; b < d->buckets; b++) {
        Dir *bucket = dir_bucket(b, seg);
        dir_free_entry(dir_bucket_row(bucket, l), s, d);
      }
    }
  }
}

void
part_clear_init(Part *d)
{
  int dir_len = part_dirlen(d);
  memset(d->raw_dir, 0, dir_len);
  part_init_dir(d);
  d->header->magic = PART_MAGIC;
  d->header->version.ink_major = CACHE_DB_MAJOR_VERSION;
  d->header->version.ink_minor = CACHE_DB_MINOR_VERSION;
  d->scan_pos = d->header->agg_pos = d->header->write_pos = d->start;
  d->header->last_write_pos = d->header->write_pos;
  d->header->phase = 0;
  d->header->cycle = 0;
  d->header->create_time = time(NULL);
  d->header->dirty = 0;
  *d->footer = *d->header;
}

int
part_dir_clear(Part *d)
{
  int dir_len = part_dirlen(d);
  part_clear_init(d);

  if (pwrite(d->fd, d->raw_dir, dir_len, d->skip) < 0) {
    Warning("unable to clear cache directory '%s'", d->hash_id);
    return -1;
  }
  return 0;
}

int
Part::clear_dir()
{
  int dir_len = part_dirlen(this);
  part_clear_init(this);

  SET_HANDLER(&Part::handle_dir_clear);

  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_buf = raw_dir;
  io.aiocb.aio_nbytes = dir_len;
  io.aiocb.aio_offset = skip;
  io.action = this;
  io.thread = AIO_CALLBACK_THREAD_ANY;
  io.then = 0;
  ink_assert(ink_aio_write(&io));
  return 0;
}

int
Part::init(char *s, ink_off_t blocks, ink_off_t dir_skip, bool clear)
{
  dir_skip = ROUND_TO_BLOCK((dir_skip < START_POS ? START_POS : dir_skip));
  path = strdup(s);
  const size_t hash_id_size = strlen(s) + 32;
  hash_id = (char *) malloc(hash_id_size);
  ink_strncpy(hash_id, s, hash_id_size);
  const size_t s_size = strlen(s);
  snprintf(hash_id + s_size, (hash_id_size - s_size), " %d:%d", (int) (dir_skip / INK_BLOCK_SIZE), (int) blocks);
  hash_id_md5.encodeBuffer(hash_id, strlen(hash_id));
  len = blocks * STORE_BLOCK_SIZE;
  ink_assert(len <= MAX_PART_SIZE);
  skip = dir_skip;
  int i;
  prev_recover_pos = 0;

  // successive approximation, directory/meta data eats up some storage
  start = dir_skip;
  part_init_data(this);
  data_blocks = (len - (start - skip)) / INK_BLOCK_SIZE;
#ifdef HIT_EVACUATE
  hit_evacuate_window = (data_blocks * cache_config_hit_evacuate_percent) / 100;
#endif

  ink_assert(sizeof(DLL<EvacuationBlock>) <= sizeof(long));
  evacuate_size = (int) (len / EVACUATION_BUCKET_SIZE) + 2;
  int evac_len = (int) evacuate_size * sizeof(DLL<EvacuationBlock>);
  evacuate = (DLL<EvacuationBlock> *)malloc(evac_len);
  memset(evacuate, 0, evac_len);

#if !defined (_WIN32)
  raw_dir = (char *) valloc(part_dirlen(this));
#else
  /* the directory should be page aligned for raw disk transfers. 
     WIN32 does not support valloc
     or memalign, so we need to allocate extra space and then align the 
     pointer ourselves. 
     Don't need to keep track of the pointer to the original memory since
     we never free this */
  size_t alignment = getpagesize();
  size_t mem_to_alloc = part_dirlen(this) + (alignment - 1);
  raw_dir = (char *) malloc(mem_to_alloc);
  raw_dir = (char *) (((unsigned int) ((char *) (raw_dir) + (alignment - 1))) & ~(alignment - 1));
#endif

  dir = (Dir *) (raw_dir + part_headerlen(this));
  header = (PartHeaderFooter *) raw_dir;
  footer = (PartHeaderFooter *) (raw_dir + part_dirlen(this) - ROUND_TO_BLOCK(sizeof(PartHeaderFooter)));

  if (clear) {
    Note("clearing cache directory '%s'", hash_id);
    return clear_dir();
  }

  init_info = new PartInitInfo();
  int footerlen = ROUND_TO_BLOCK(sizeof(PartHeaderFooter));
  ink_off_t footer_offset = part_dirlen(this) - footerlen;
  // try A
  ink_off_t as = skip;
  if (is_debug_tag_set("cache_init"))
    Note("reading directory '%s'", hash_id);
  SET_HANDLER(&Part::handle_header_read);
  init_info->part_aio[0].aiocb.aio_offset = as;
  init_info->part_aio[1].aiocb.aio_offset = as + footer_offset;
  ink_off_t bs = skip + part_dirlen(this);
  init_info->part_aio[2].aiocb.aio_offset = bs;
  init_info->part_aio[3].aiocb.aio_offset = bs + footer_offset;

  for (i = 0; i < 4; i++) {
    AIOCallback *aio = &(init_info->part_aio[i]);
    aio->aiocb.aio_fildes = fd;
    aio->aiocb.aio_buf = &(init_info->part_h_f[i * INK_BLOCK_SIZE]);
    aio->aiocb.aio_nbytes = footerlen;
    aio->action = this;
    aio->thread = this_ethread();
    aio->then = (i < 3) ? &(init_info->part_aio[i + 1]) : 0;
  }

  eventProcessor.schedule_imm(this, ET_CALL);
  return 0;
}

int
Part::handle_dir_clear(int event, void *data)
{
  int dir_len = part_dirlen(this);
  AIOCallback *op;

  if (event == AIO_EVENT_DONE) {
    op = (AIOCallback *) data;
    if ((int) op->aio_result != (int) op->aiocb.aio_nbytes) {
      Warning("unable to clear cache directory '%s'", hash_id);
      fd = -1;
    }

    if ((int) op->aiocb.aio_nbytes == dir_len) {
      /* clear the header for directory B. We don't need to clear the
         whole of directory B. The header for directory B starts at
         skip + len */
      op->aiocb.aio_nbytes = ROUND_TO_BLOCK(sizeof(PartHeaderFooter));
      op->aiocb.aio_offset = skip + dir_len;
      ink_assert(ink_aio_write(op));
      return EVENT_DONE;
    }
    set_io_not_in_progress();
    SET_HANDLER(&Part::dir_init_done);
    dir_init_done(EVENT_IMMEDIATE, 0);
    /* mark the partition as bad */
  }
  return EVENT_DONE;
}

int
Part::handle_dir_read(int event, void *data)
{
  AIOCallback *op = (AIOCallback *) data;

  if (event == AIO_EVENT_DONE) {
    if ((int) op->aio_result != (int) op->aiocb.aio_nbytes) {
      clear_dir();
      return EVENT_DONE;
    }
  }

  if (header->magic != PART_MAGIC || header->version.ink_major != CACHE_DB_MAJOR_VERSION || footer->magic != PART_MAGIC) {
    Warning("bad footer in cache directory for '%s', clearing", hash_id);
    Note("clearing cache directory '%s'", hash_id);
    clear_dir();
    return EVENT_DONE;
  }
  CHECK_DIR(this);

  SET_HANDLER(&Part::handle_recover_from_data);
  return handle_recover_from_data(EVENT_IMMEDIATE, 0);
}

/* 
   Philosophy:  The idea is to find the region of disk that could be 
   inconsistent and remove all directory entries pointing to that potentially
   inconsistent region. 
   Start from a consistent position (the write_pos of the last directory 
   synced to disk) and scan forward. Two invariants for docs that were
   written to the disk after the directory was synced:
 
   1. doc->magic == DOC_MAGIC 

   The following two cases happen only when the previous generation 
   documents are aligned with the current ones. 

   2. All the docs written to the disk
   after the directory was synced will have their sync_serial <=
   header->sync_serial + 1,  because the write aggregation can take 
   indeterminate amount of time to sync. The doc->sync_serial can be 
   equal to header->sync_serial + 1, because we increment the sync_serial
   before we sync the directory to disk.

   3. The doc->sync_serial will always increase. If doc->sync_serial 
   decreases, the document was written in the previous phase
   
   If either of these conditions fail and we are not too close to the end 
   (see the next comment ) then we're done

   We actually start from header->last_write_pos instead of header->write_pos
   to make sure that we haven't wrapped around the whole disk without
   syncing the directory.  Since the sync serial is 60 seconds, it is
   entirely possible to write through the whole cache without
   once syncing the directory. In this case, we need to clear the 
   cache.The documents written right before we synced the
   directory to disk should have the write_serial <= header->sync_serial.

      */

int
Part::handle_recover_from_data(int event, void *data)
{
  (void) data;
  int got_len = 0;
  inku32 max_sync_serial = header->sync_serial;
  char *s, *e;
  if (event == EVENT_IMMEDIATE) {
    if (header->sync_serial == 0) {
      io.aiocb.aio_buf = NULL;
      SET_HANDLER(&Part::handle_recover_write_dir);
      return handle_recover_write_dir(EVENT_IMMEDIATE, 0);
    }
    // initialize
    recover_wrapped = 0;
    last_sync_serial = 0;
    last_write_serial = 0;
    recover_pos = header->last_write_pos;
    if (recover_pos >= skip + len) {
      recover_wrapped = 1;
      recover_pos = start;
    }
#if defined(_WIN32)
    io.aiocb.aio_buf = (char *) malloc(RECOVERY_SIZE);
#else
    io.aiocb.aio_buf = (char *) valloc(RECOVERY_SIZE);
#endif
    io.aiocb.aio_nbytes = RECOVERY_SIZE;
    if ((ink_off_t)(recover_pos + io.aiocb.aio_nbytes) > (ink_off_t)(skip + len))
      io.aiocb.aio_nbytes = (skip + len) - recover_pos;
  } else if (event == AIO_EVENT_DONE) {
    if ((int) io.aiocb.aio_nbytes != (int) io.aio_result) {
      Warning("disk read error on recover '%s', clearing", hash_id);
      goto Lclear;
    }
    if (io.aiocb.aio_offset == header->last_write_pos) {

      /* check that we haven't wrapped around without syncing
         the directory. Start from last_write_serial (write pos the documents
         were written to just before syncing the directory) and make sure
         that all documents have write_serial <= header->write_serial.
       */
      int to_check = header->write_pos - header->last_write_pos;
      ink_assert(to_check && to_check < (int) io.aiocb.aio_nbytes);
      int done = 0;
      s = (char *) io.aiocb.aio_buf;
      while (done < to_check) {
        Doc *doc = (Doc *) (s + done);
        if (doc->magic != DOC_MAGIC || doc->write_serial > header->write_serial) {
          Warning("no valid directory found while recovering '%s', clearing", hash_id);
          goto Lclear;
        }
        done += round_to_approx_size(doc->len);
        if (doc->sync_serial > last_write_serial)
          last_sync_serial = doc->sync_serial;
      }
      ink_assert(done == to_check);

      got_len = io.aiocb.aio_nbytes - done;
      recover_pos += io.aiocb.aio_nbytes;
      s = (char *) io.aiocb.aio_buf + done;
      e = s + got_len;
    } else {
      got_len = io.aiocb.aio_nbytes;
      recover_pos += io.aiocb.aio_nbytes;
      s = (char *) io.aiocb.aio_buf;
      e = s + got_len;
    }
  }
  // examine what we got
  if (got_len) {

    Doc *doc = NULL;
    Doc *last_doc = NULL;

    if (recover_wrapped && start == io.aiocb.aio_offset) {
      doc = (Doc *) s;
      if (doc->magic != DOC_MAGIC || doc->write_serial < last_write_serial) {
        recover_pos = skip + len - EVACUATION_SIZE;
        goto Ldone;
      }
    }

    while (s < e) {
      doc = (Doc *) s;

      if (doc->magic != DOC_MAGIC || doc->sync_serial != last_sync_serial) {

        if (doc->magic == DOC_MAGIC) {
          if (doc->sync_serial > header->sync_serial)
            max_sync_serial = doc->sync_serial;

          /* 
             doc->magic == DOC_MAGIC, but doc->sync_serial != last_sync_serial
             This might happen in the following situations 
             1. We are starting off recovery. In this case the 
             last_sync_serial == header->sync_serial, but the doc->sync_serial
             can be anywhere in the range (0, header->sync_serial + 1]
             If this is the case, update last_sync_serial and continue;

             2. A dir sync started between writing documents to the 
             aggregation buffer and hence the doc->sync_serial went up. 
             If the doc->sync_serial is greater than the last
             sync serial and less than (header->sync_serial + 2) then 
             continue;

             3. If the position we are recovering from is within AGG_SIZE
             from the disk end, then we can't trust this document. The 
             aggregation buffer might have been larger than the remaining space
             at the end and we decided to wrap around instead of writing 
             anything at that point. In this case, wrap around and start
             from the beginning. 

             If neither of these 3 cases happen, then we are indeed done. 

           */

          // case 1
          // case 2
          if (doc->sync_serial > last_sync_serial && doc->sync_serial <= header->sync_serial + 1) {
            last_sync_serial = doc->sync_serial;
            s += round_to_approx_size(doc->len);
            continue;
          }
          // case 3 - we have already recoverd some data and  
          // (doc->sync_serial < last_sync_serial) ||
          // (doc->sync_serial > header->sync_serial + 1). 
          // if we are too close to the end, wrap around
          else if (recover_pos - (e - s) > (skip + len) - AGG_SIZE) {
            recover_wrapped = 1;
            recover_pos = start;
            io.aiocb.aio_nbytes = RECOVERY_SIZE;

            break;
          }
          // we are done. This doc was written in the earlier phase
          recover_pos -= e - s;
          goto Ldone;
        } else {
          // doc->magic != DOC_MAGIC
          // If we are in the danger zone - recover_pos is within AGG_SIZE
          // from the end, then wrap around 
          recover_pos -= e - s;
          if (recover_pos > (skip + len) - AGG_SIZE) {
            recover_wrapped = 1;
            recover_pos = start;
            io.aiocb.aio_nbytes = RECOVERY_SIZE;

            break;
          }
          // we ar not in the danger zone
          goto Ldone;
        }
      }
      // doc->magic == DOC_MAGIC && doc->sync_serial == last_sync_serial
      last_write_serial = doc->write_serial;
      last_doc = doc;
      s += round_to_approx_size(doc->len);
    }

    /* if (s > e) then we gone through RECOVERY_SIZE; we need to 
       read more data off disk and continue recovering */
    if (s >= e) {
      /* In the last iteration, we increment s by doc->len...need to undo 
         that change */
      if (s > e)
        s -= round_to_approx_size(doc->len);
      recover_pos -= e - s;
      if (recover_pos >= skip + len)
        recover_pos = start;
      io.aiocb.aio_nbytes = RECOVERY_SIZE;
      if ((ink_off_t)(recover_pos + io.aiocb.aio_nbytes) > (ink_off_t)(skip + len))
        io.aiocb.aio_nbytes = (skip + len) - recover_pos;
    }
  }
  if (recover_pos == prev_recover_pos) // this should never happen, but if it does break the loop
    goto Lclear;
  prev_recover_pos = recover_pos;
  io.aiocb.aio_offset = recover_pos;
  ink_assert(ink_aio_read(&io));
  return EVENT_CONT;

Ldone:{
    /* if we come back to the starting position, then we don't have to recover anything */
    if (recover_pos == header->write_pos && recover_wrapped) {
      SET_HANDLER(&Part::handle_recover_write_dir);
      if (is_debug_tag_set("cache_init"))
        Note("recovery wrapped around. nothing to clear\n");
      return handle_recover_write_dir(EVENT_IMMEDIATE, 0);
    }

    recover_pos += EVACUATION_SIZE;   // safely cover the max write size
    if (recover_pos < header->write_pos && (recover_pos + EVACUATION_SIZE >= header->write_pos)) {
      Debug("cache_init", "Head Pos: %llu, Rec Pos: %llu, Wrapped:%d", header->write_pos, recover_pos, recover_wrapped);
      Warning("no valid directory found while recovering '%s', clearing", hash_id);
      goto Lclear;
    }

    if (recover_pos > skip + len)
      recover_pos -= skip + len;
    // bump sync number so it is different from that in the Doc structs
    inku32 next_sync_serial = max_sync_serial + 1;
    // make that the next sync does not overwrite our good copy!
    if (!(header->sync_serial & 1) == !(next_sync_serial & 1))
      next_sync_serial++;
    // clear effected portion of the cache
    ink_off_t clear_start = offset_to_part_offset(this, header->write_pos);
    ink_off_t clear_end = offset_to_part_offset(this, recover_pos);
    if (clear_start <= clear_end)
      dir_clear_range(clear_start, clear_end, this);
    else {
      dir_clear_range(clear_end, DIR_OFFSET_MAX, this);
      dir_clear_range(1, clear_start, this);
    }
    if (is_debug_tag_set("cache_init"))
      Note("recovery clearing offsets [%llu, %llu] sync_serial %d next %d\n",
           header->write_pos, recover_pos, header->sync_serial, next_sync_serial);
    footer->sync_serial = header->sync_serial = next_sync_serial;

    for (int i = 0; i < 3; i++) {
      AIOCallback *aio = &(init_info->part_aio[i]);
      aio->aiocb.aio_fildes = fd;
      aio->action = this;
      aio->thread = AIO_CALLBACK_THREAD_ANY;
      aio->then = (i < 2) ? &(init_info->part_aio[i + 1]) : 0;
    }
    int footerlen = ROUND_TO_BLOCK(sizeof(PartHeaderFooter));
    int dirlen = part_dirlen(this);
    int B = header->sync_serial & 1;
    ink_off_t ss = skip + (B ? dirlen : 0);

    init_info->part_aio[0].aiocb.aio_buf = raw_dir;
    init_info->part_aio[0].aiocb.aio_nbytes = footerlen;
    init_info->part_aio[0].aiocb.aio_offset = ss;
    init_info->part_aio[1].aiocb.aio_buf = raw_dir + footerlen;
    init_info->part_aio[1].aiocb.aio_nbytes = dirlen - 2 * footerlen;
    init_info->part_aio[1].aiocb.aio_offset = ss + footerlen;
    init_info->part_aio[2].aiocb.aio_buf = raw_dir + dirlen - footerlen;
    init_info->part_aio[2].aiocb.aio_nbytes = footerlen;
    init_info->part_aio[2].aiocb.aio_offset = ss + dirlen - footerlen;

    SET_HANDLER(&Part::handle_recover_write_dir);
    ink_assert(ink_aio_write(init_info->part_aio));
    return EVENT_CONT;
  }

Lclear:
  free((char *) io.aiocb.aio_buf);
  delete init_info;
  init_info = 0;
  clear_dir();
  return EVENT_CONT;
}

int
Part::handle_recover_write_dir(int event, void *data)
{
  (void) event;
  (void) data;
  if (io.aiocb.aio_buf)
    free((char *) io.aiocb.aio_buf);
  delete init_info;
  init_info = 0;
  set_io_not_in_progress();
  scan_pos = header->write_pos;
  periodic_scan();
  SET_HANDLER(&Part::dir_init_done);
  return dir_init_done(EVENT_IMMEDIATE, 0);
}

int
Part::handle_header_read(int event, void *data)
{
  AIOCallback *op;
  PartHeaderFooter *hf[4];
  switch (event) {
  case EVENT_IMMEDIATE:
  case EVENT_INTERVAL:
    ink_assert(ink_aio_read(init_info->part_aio));
    return EVENT_CONT;

  case AIO_EVENT_DONE:
    op = (AIOCallback *) data;
    for (int i = 0; i < 4; i++) {
      ink_assert(op != 0);
      hf[i] = (PartHeaderFooter *) (op->aiocb.aio_buf);
      if ((int) op->aio_result != (int) op->aiocb.aio_nbytes) {
        clear_dir();
        return EVENT_DONE;
      }
      op = op->then;
    }

    io.aiocb.aio_fildes = fd;
    io.aiocb.aio_nbytes = part_dirlen(this);
    io.aiocb.aio_buf = raw_dir;
    io.action = this;
    io.thread = AIO_CALLBACK_THREAD_ANY;
    io.then = 0;

    if (hf[0]->sync_serial == hf[1]->sync_serial &&
        (hf[0]->sync_serial >= hf[2]->sync_serial || hf[2]->sync_serial != hf[3]->sync_serial)) {
      SET_HANDLER(&Part::handle_dir_read);
      if (is_debug_tag_set("cache_init"))
        Note("using directory A for '%s'", hash_id);
      io.aiocb.aio_offset = skip;
      ink_assert(ink_aio_read(&io));
    }
    // try B
    else if (hf[2]->sync_serial == hf[3]->sync_serial) {

      SET_HANDLER(&Part::handle_dir_read);
      if (is_debug_tag_set("cache_init"))
        Note("using directory B for '%s'", hash_id);
      io.aiocb.aio_offset = skip + part_dirlen(this);
      ink_assert(ink_aio_read(&io));
    } else {
      Note("no good directory, clearing '%s'", hash_id);
      clear_dir();
      delete init_info;
      init_info = 0;
    }

    return EVENT_DONE;
  }
  return EVENT_DONE;
}

int
Part::dir_init_done(int event, void *data)
{
  (void) event;
  (void) data;
  if (!cache->cache_read_done) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(5), ET_CALL);
    return EVENT_CONT;
  } else {
    int part_no = ink_atomic_increment(&gnpart, 1);
    ink_assert(!gpart[part_no]);
    gpart[part_no] = this;
    SET_HANDLER(&Part::aggWrite);
    if (fd == -1)
      cache->part_initialized(0);
    else
      cache->part_initialized(1);
    return EVENT_DONE;
  }
}

void
build_part_hash_table(CacheHostRecord *cp)
{
  int num_parts = cp->num_part;
  unsigned int *mapping = (unsigned int *) xmalloc(sizeof(unsigned int) * num_parts);
  Part **p = (Part **) xmalloc(sizeof(Part *) * num_parts);

  memset(mapping, 0, num_parts * sizeof(unsigned int));
  memset(p, 0, num_parts * sizeof(Part *));
  inku64 total = 0;
  int i = 0;
  int used = 0;
  int bad_parts = 0;
  int map = 0;
  // initialize number of elements per part
  for (i = 0; i < num_parts; i++) {
    if (DISK_BAD(cp->parts[i]->disk)) {
      bad_parts++;
      continue;
    }
    mapping[map] = i;
    p[map++] = cp->parts[i];
    total += (cp->parts[i]->len >> INK_BLOCK_SHIFT);
  }

  num_parts -= bad_parts;

  if (!num_parts) {
    // all the disks are corrupt, 
    if (cp->part_hash_table) {
      new_Freer(cp->part_hash_table, CACHE_MEM_FREE_TIMEOUT);
    }
    cp->part_hash_table = NULL;
    xfree(mapping);
    xfree(p);
    return;
  }


  unsigned int *forpart = (unsigned int *) alloca(sizeof(unsigned int) * num_parts);
  unsigned int *rnd = (unsigned int *) alloca(sizeof(unsigned int) * num_parts);
  unsigned short *ttable = (unsigned short *) xmalloc(sizeof(unsigned short) * PART_HASH_TABLE_SIZE);

  for (i = 0; i < num_parts; i++) {
    forpart[i] = (PART_HASH_TABLE_SIZE * (p[i]->len >> INK_BLOCK_SHIFT)) / total;
    used += forpart[i];
  }
  // spread around the excess
  int extra = PART_HASH_TABLE_SIZE - used;
  for (i = 0; i < extra; i++) {
    forpart[i % num_parts]++;
  }
  // seed random number generator
  for (i = 0; i < num_parts; i++) {
    inku64 x = p[i]->hash_id_md5.fold();
    rnd[i] = (unsigned int) x;
  }
  // initialize table to "empty"
  for (i = 0; i < PART_HASH_TABLE_SIZE; i++)
    ttable[i] = PART_HASH_EMPTY;
  // give each machine it's fav
  int left = PART_HASH_TABLE_SIZE;
  int d = 0;
  for (; left; d = (d + 1) % num_parts) {
    if (!forpart[d])
      continue;
    do {
      i = next_rand(&rnd[d]) % PART_HASH_TABLE_SIZE;
    } while (ttable[i] != PART_HASH_EMPTY);
    ttable[i] = mapping[d];
    forpart[d]--;
    left--;
  }

  // install new table

  if (cp->part_hash_table) {
    new_Freer(cp->part_hash_table, CACHE_MEM_FREE_TIMEOUT);
  }
  xfree(mapping);
  xfree(p);
  cp->part_hash_table = ttable;
}


void
Cache::part_initialized(bool result)
{
  ink_atomic_increment(&total_initialized_part, 1);
  if (result)
    ink_atomic_increment(&total_good_npart, 1);
  if (total_npart == total_initialized_part)
    open_done();
}

int
AIO_Callback_handler::handle_disk_failure(int event, void *data)
{
  (void) event;
  /* search for the matching file descriptor */
  if (!CacheProcessor::cache_ready)
    return EVENT_DONE;
  int disk_no = 0;
  int good_disks = 0;
  AIOCallback *cb = (AIOCallback *) data;
  for (; disk_no < gndisks; disk_no++) {
    CacheDisk *d = gdisks[disk_no];

    if (d->fd == cb->aiocb.aio_fildes) {
      d->num_errors++;

      if (!DISK_BAD(d)) {

        char message[128];
        snprintf(message, sizeof(message), "Error accessing Disk %s", d->path);
        Warning(message);
        IOCORE_SignalManager(REC_SIGNAL_CACHE_WARNING, message);
      } else if (!DISK_BAD_SIGNALLED(d)) {

        char message[128];
        snprintf(message, sizeof(message), "too many errors accessing disk %s: declaring disk bad", d->path);
        Warning(message);
        IOCORE_SignalManager(REC_SIGNAL_CACHE_ERROR, message);
        /* subtract the disk space that was being used from  the cache size stat */
        // dir entries stat 
        int p;
        inku64 total_bytes_delete = 0;
        inku64 total_dir_delete = 0;
        inku64 used_dir_delete = 0;

        for (p = 0; p < gnpart; p++) {
          if (d->fd == gpart[p]->fd) {
            total_dir_delete += gpart[p]->buckets * gpart[p]->segments * DIR_DEPTH;
            used_dir_delete += dir_entries_used(gpart[p]);
            total_bytes_delete = gpart[p]->len - part_dirlen(gpart[p]);
          }
        }

        RecIncrGlobalRawStat(cache_rsb, cache_bytes_total_stat, -total_bytes_delete);
        RecIncrGlobalRawStat(cache_rsb, cache_bytes_total_stat, -total_dir_delete);
        RecIncrGlobalRawStat(cache_rsb, cache_bytes_total_stat, -cache_direntries_used_stat);

        if (theCache) {
          rebuild_host_table(theCache);
        }
        if (theStreamCache) {
          rebuild_host_table(theStreamCache);
        }
      }
      if (good_disks)
        return EVENT_DONE;
    }

    if (!DISK_BAD(d))
      good_disks++;

  }
  if (!good_disks) {
    Warning("all disks are bad, cache disabled");
    CacheProcessor::cache_ready = 0;
    delete cb;
    return EVENT_DONE;
  }

  if (theCache && !theCache->hosttable->gen_host_rec.part_hash_table) {
    unsigned int caches_ready = 0;
    caches_ready = caches_ready | CACHE_FRAG_TYPE_HTTP;
    caches_ready = caches_ready | CACHE_FRAG_TYPE_NONE;
    caches_ready = caches_ready | CACHE_FRAG_TYPE_NNTP;
    caches_ready = caches_ready | CACHE_FRAG_TYPE_FTP;
    caches_ready = ~caches_ready;
    CacheProcessor::cache_ready &= caches_ready;
    Warning("all partitions for http cache are corrupt, http cache disabled");
  }
  if (theStreamCache && !theStreamCache->hosttable->gen_host_rec.part_hash_table) {
    unsigned int caches_ready = 0;
    caches_ready = caches_ready | CACHE_FRAG_TYPE_RTSP;
    caches_ready = ~caches_ready;
    CacheProcessor::cache_ready &= caches_ready;
    Warning("all partitions for mixt cache are corrupt, mixt cache disabled");
  }
  delete cb;
  return EVENT_DONE;
}

int
Cache::open_done()
{
#ifdef NON_MODULAR
  Action *register_ShowCache(Continuation * c, HTTPHdr * h);
  Action *register_ShowCacheInternal(Continuation *c, HTTPHdr *h);
  statPagesManager.register_http("cache", register_ShowCache);
  statPagesManager.register_http("cache-internal", register_ShowCacheInternal);
#endif
  if (total_good_npart == 0) {
    ready = CACHE_INIT_FAILED;
    cacheProcessor.cacheInitialized();
    return 0;
  }

  hosttable = NEW(new CacheHostTable(this, scheme));
  hosttable->register_config_callback(&hosttable);

  if (hosttable->gen_host_rec.num_cachepart == 0)
    ready = CACHE_INIT_FAILED;
  else
    ready = CACHE_INITIALIZED;
  cacheProcessor.cacheInitialized();

  return 0;
}

int
Cache::open(bool clear, bool fix)
{
  NOWARN_UNUSED(fix);
  int i;
  ink_off_t blocks;
  cache_read_done = 0;
  total_initialized_part = 0;
  total_npart = 0;
  total_good_npart = 0;


  IOCORE_EstablishStaticConfigInt32(cache_config_min_average_object_size, "proxy.config.cache.min_average_object_size");
  Debug("cache_init", "Cache::open - proxy.config.cache.min_average_object_size = %ld",
        (long) cache_config_min_average_object_size);

  CachePart *cp = cp_list.head;
  for (; cp; cp = cp->link.next) {
    if (cp->scheme == scheme) {
      cp->parts = (Part **) xmalloc(cp->num_parts * sizeof(Part *));
      int part_no = 0;
      for (i = 0; i < gndisks; i++) {
        if (cp->disk_parts[i] && !DISK_BAD(cp->disk_parts[i]->disk)) {
          DiskPartBlockQueue *q = cp->disk_parts[i]->dpb_queue.head;
          for (; q; q = q->link.next) {
            cp->parts[part_no] = NEW(new Part());
            CacheDisk *d = cp->disk_parts[i]->disk;
            cp->parts[part_no]->disk = d;
            cp->parts[part_no]->fd = d->fd;
            cp->parts[part_no]->cache = this;
            cp->parts[part_no]->cache_part = cp;
            blocks = q->b->len;

            bool part_clear = clear || d->cleared || q->new_block;
            cp->parts[part_no]->init(d->path, blocks, q->b->offset, part_clear);
            part_no++;
            cache_size += blocks;
          }
        }
      }
      total_npart += part_no;
    }
  }
  if (total_npart == 0)
    return open_done();
  cache_read_done = 1;
  return 0;
}


int
Cache::close()
{
  return -1;
}

int
CacheVC::dead(int event, Event *e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);
  ink_assert(0);
  return EVENT_DONE;
}

#define STORE_COLLISION 1

int
CacheVC::handleReadDone(int event, Event *e)
{
  NOWARN_UNUSED(e);
  cancel_trigger();
  ink_debug_assert(this_ethread() == mutex->thread_holding);

  if (event == AIO_EVENT_DONE)
    set_io_not_in_progress();
  else
    if (is_io_in_progress())
      return EVENT_CONT;
  {
    MUTEX_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock)
      VC_SCHED_LOCK_RETRY();
    if ((!dir_valid(part, &dir)) || (!io.ok())) {
      if (!io.ok()) {
        Debug("cache_disk_error", "Read error on disk %s\n \
	    read range : [%llu - %llu bytes]  [%llu - %llu blocks] \n", part->hash_id, io.aiocb.aio_offset, io.aiocb.aio_offset + io.aiocb.aio_nbytes, io.aiocb.aio_offset / 512, (io.aiocb.aio_offset + io.aiocb.aio_nbytes) / 512);
      }
      goto Ldone;
    }

    ink_assert(part->mutex->nthread_holding < 1000);
    ink_assert(((Doc *) buf->data())->magic == DOC_MAGIC);
#ifdef VERIFY_JTEST_DATA
    char xx[500];
    if (read_key && *read_key == ((Doc *) buf->data())->key && request.valid() && !dir_head(&dir) && !vio.ndone) {
      int ib = 0, xd = 0;
      request.url_get()->print(xx, 500, &ib, &xd);
      char *x = xx;
      for (int q = 0; q < 3; q++)
        x = strchr(x + 1, '/');
      ink_assert(!memcmp(((Doc *) buf->data())->data(), x, ib - (x - xx)));
    }
#endif
    Doc *doc = (Doc *) buf->data();
    // put into ram cache?
    if (io.ok() &&
        ((doc->first_key == *read_key) || (doc->key == *read_key) || STORE_COLLISION) && doc->magic == DOC_MAGIC) {
      int okay = 1;
      f.not_from_ram_cache = 1;
      if (cache_config_enable_checksum && doc->checksum != DOC_NO_CHECKSUM) {
        // verify that the checksum matches
        inku32 checksum = 0;
        for (char *b = doc->hdr(); b < (char *) doc + doc->len; b++)
          checksum += *b;
        ink_assert(checksum == doc->checksum);
        if (checksum != doc->checksum) {
          Note("cache: checksum error for [%llu %llu] len %d, hlen %d, disk %s, offset %llu size %d",
               doc->first_key.b[0], doc->first_key.b[1],
               doc->len, doc->hlen, part->path, io.aiocb.aio_offset, io.aiocb.aio_nbytes);
          doc->magic = DOC_CORRUPT;
          okay = 0;
        }
      }
      // If http doc, we need to unmarshal the headers before putting
      // in the ram cache. 
#ifdef HTTP_CACHE
      if (doc->ftype == CACHE_FRAG_TYPE_HTTP && doc->hlen && okay) {
        char *tmp = doc->hdr();
        int len = doc->hlen;
        while (len > 0) {
          int r = HTTPInfo::unmarshal(tmp, len, buf._ptr());
          if (r < 0) {
            ink_assert(!"CacheVC::handleReadDone unmarshal failed");
            okay = 0;
            break;
          }
          len -= r;
          tmp += r;
        }
      }
#endif
      // Put the request in the ram cache only if its a open_read or lookup
      if (vio.op == VIO::READ && okay) {
        bool cutoff_check;
        // cutoff_check : 
        // doc_len == 0 for the first fragment (it is set from the vector)
        //                The decision on the first fragment is based on 
        //                doc->total_len
        // After that, the decision is based of doc_len (doc_len != 0)
        // (cache_config_ram_cache_cutoff == 0) : no cutoffs
        cutoff_check = ((!doc_len && doc->total_len < part->ram_cache.cutoff_size)
                        || (doc_len && doc_len < part->ram_cache.cutoff_size)
                        || !part->ram_cache.cutoff_size);
        if (cutoff_check)
          part->ram_cache.put(read_key, buf, mutex->thread_holding, 0, dir_offset(&dir));
        if (!doc_len) {
          // keep a pointer to it. In case the state machine decides to
          // update this document, we don't have to read it back in memory
          // again
          part->first_fragment.key = *read_key;
          part->first_fragment.auxkey1 = dir_offset(&dir);
          part->first_fragment.data = buf;
        }
      }                           // end VIO::READ check
    }                             // end io.ok() check
  }
Ldone:
  POP_HANDLER;
  return handleEvent(AIO_EVENT_DONE, 0);
}


int
CacheVC::handleRead(int event, Event *e)
{
  NOWARN_UNUSED(e);
  cancel_trigger();

  // check ram cache
  ink_debug_assert(part->mutex->thread_holding == this_ethread());
  if (part->ram_cache.get(read_key, &buf, 0, dir_offset(&dir))) {
    CACHE_INCREMENT_DYN_STAT(cache_ram_cache_hits_stat);
    goto LramHit;
  }
  // check if it was read in the last open_read call.
  if (*read_key == part->first_fragment.key && dir_offset(&dir) == part->first_fragment.auxkey1) {
    buf = part->first_fragment.data;
    goto LramHit;
  }

  CACHE_INCREMENT_DYN_STAT(cache_ram_cache_misses_stat);

  // see if its in the aggregation buffer
  if (dir_agg_buf_valid(part, &dir)) {
    int agg_offset = part_offset(part, &dir) - part->header->write_pos;
    buf = new_IOBufferData(iobuffer_size_to_index(io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
    ink_assert((agg_offset + io.aiocb.aio_nbytes) <= (unsigned) part->agg_buf_pos);
    char *doc = buf->data();
    char *agg = part->agg_buffer + agg_offset;
    memcpy(doc, agg, io.aiocb.aio_nbytes);
    io.aio_result = io.aiocb.aio_nbytes;
    SET_HANDLER(&CacheVC::handleReadDone);
    return EVENT_RETURN;
  }

  io.aiocb.aio_fildes = part->fd;
  io.aiocb.aio_offset = part_offset(part, &dir);
  if ((ink_off_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > (ink_off_t)(part->skip + part->len))
    io.aiocb.aio_nbytes = part->skip + part->len - io.aiocb.aio_offset;
  buf = new_IOBufferData(iobuffer_size_to_index(io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  io.aiocb.aio_buf = buf->data();
  io.action = this;
  io.thread = mutex->thread_holding;
  SET_HANDLER(&CacheVC::handleReadDone);
  ink_assert(ink_aio_read(&io) >= 0);
  CACHE_DEBUG_INCREMENT_DYN_STAT(cache_pread_count_stat);
  return EVENT_CONT;

LramHit:
  io.aio_result = io.aiocb.aio_nbytes;
  POP_HANDLER;
  return EVENT_RETURN; // allow the caller to release the partition lock
}

Action *
Cache::lookup(Continuation *cont, CacheKey *key, CacheFragType type, char *hostname, int host_len)
{

  if (!(CacheProcessor::cache_ready & type)) {
    cont->handleEvent(CACHE_EVENT_LOOKUP_FAILED, 0);
    return ACTION_RESULT_DONE;
  }

  Part *part = key_to_part(key, hostname, host_len);
  ProxyMutex *mutex = cont->mutex;
  CacheVC *c = new_CacheVC(cont);
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
  c->vio.op = VIO::READ;
  c->base_stat = cache_lookup_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->first_key = c->key = *key;
  c->frag_type = type;
  c->f.lookup = 1;
  c->part = part;
  c->last_collision = NULL;

  if (c->handleEvent(EVENT_INTERVAL, 0) == EVENT_CONT)
    return &c->_action;
  else
    return ACTION_RESULT_DONE;
}

#ifdef HTTP_CACHE
Action *
Cache::lookup(Continuation *cont, CacheURL *url, CacheFragType type)
{
  INK_MD5 md5;

  url->MD5_get(&md5);
  int len = 0;
  const char *hostname = url->host_get(&len);
  return lookup(cont, &md5, type, (char *) hostname, len);
}
#endif

int
CacheVC::removeEvent(int event, Event *e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  cancel_trigger();
  set_io_not_in_progress();
  {
    MUTEX_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock)
      VC_SCHED_LOCK_RETRY();
    if (_action.cancelled) {
      if (od) {
        part->close_write(this);
        od = 0;
      }
      goto Lfree;
    }
    if (!f.remove_aborted_writers) {
      if (part->open_write(this, true, 1)) {
        // writer  exists
        ink_assert(od = part->open_read(&key));
        od->dont_update_directory = 1;
        od = NULL;
      } else {
        od->dont_update_directory = 1;
      }
      f.remove_aborted_writers = 1;
    }
  Lread:
    if (!buf)
      goto Lcollision;
    if (!dir_valid(part, &dir)) {
      last_collision = NULL;
      goto Lcollision;
    }
    // check read completed correct FIXME: remove bad parts
    if ((int) io.aio_result != (int) io.aiocb.aio_nbytes)
      goto Ldone;
    {
      // verify that this is our document
      Doc *doc = (Doc *) buf->data();
      /* should be first_key not key..right?? */
      if (doc->first_key == key) {
        ink_assert(doc->magic == DOC_MAGIC);
        if (dir_delete(&key, part, &dir) > 0) {
          if (od)
            part->close_write(this);
          od = NULL;
          goto Lremoved;
        }
        goto Ldone;
      }
    }
  Lcollision:
    // check for collision
    if (dir_probe(&key, part, &dir, &last_collision) > 0) {
      int ret = do_read_call(&key);
      if (ret == EVENT_RETURN)
        goto Lread;
      return ret;
    }
  Ldone:
    CACHE_INCREMENT_DYN_STAT(cache_remove_failure_stat);
    if (od)
      part->close_write(this);
  }
  ink_debug_assert(!part || this_ethread() != part->mutex->thread_holding);
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE_FAILED, (void *) -ECACHE_NO_DOC);
  goto Lfree;
Lremoved:
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE, 0);
Lfree:
  return free_CacheVC(this);
}

Action *
Cache::remove(Continuation *cont, CacheKey *key, CacheFragType type, 
              bool user_agents, bool link, 
              char *hostname, int host_len)
{
  NOWARN_UNUSED(user_agents);
  NOWARN_UNUSED(link);

  if (!(CacheProcessor::cache_ready & type)) {
    if (cont)
      cont->handleEvent(CACHE_EVENT_REMOVE_FAILED, 0);
    return ACTION_RESULT_DONE;
  }

  ink_assert(this);

  ProxyMutexPtr mutex = NULL;
  if (!cont)
    cont = new_CacheRemoveCont();

  CACHE_TRY_LOCK(lock, cont->mutex, this_ethread());
  ink_assert(lock);
  Part *part = key_to_part(key, hostname, host_len);
  // coverity[var_decl]
  Dir result;
  dir_clear(&result);           // initialized here, set result empty so we can recognize missed lock
  mutex = cont->mutex;

  CacheVC *c = new_CacheVC(cont);
  c->vio.op = VIO::NONE;
  c->frag_type = type;
  c->base_stat = cache_remove_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->first_key = c->key = *key;
  c->part = part;
  c->dir = result;
  c->f.remove = 1;

  SET_CONTINUATION_HANDLER(c, &CacheVC::removeEvent);
  int ret = c->removeEvent(EVENT_IMMEDIATE, 0);
  if (ret == EVENT_DONE)
    return ACTION_RESULT_DONE;
  else
    return &c->_action;
}

#ifdef HTTP_CACHE
Action *
Cache::remove(Continuation *cont, CacheURL *url, CacheFragType type)
{
  INK_MD5 md5;
  url->MD5_get(&md5);
  int host_len = 0;
  const char *hostname = url->host_get(&host_len);
  return remove(cont, &md5, type, true, false, (char *) hostname, host_len);
}
#endif

// CacheVConnection

CacheVConnection::CacheVConnection()
:VConnection(NULL)
{
}

void
cplist_init()
{
  int i;
  unsigned int j;
  cp_list_len = 0;
  for (i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    DiskPart **dp = d->disk_parts;
    for (j = 0; j < d->header->num_partitions; j++) {
      ink_assert(dp[j]->dpb_queue.head);
      CachePart *p = cp_list.head;
      while (p) {
        if (p->part_number == dp[j]->part_number) {
          ink_assert(p->scheme == (int) dp[j]->dpb_queue.head->b->type);
          p->size += dp[j]->size;
          p->num_parts += dp[j]->num_partblocks;
          p->disk_parts[i] = dp[j];
          break;
        }
        p = p->link.next;
      }
      if (!p) {
        // did not find a partition in the cache part list...create
        // a new one
        CachePart *new_p = NEW(new CachePart());
        new_p->part_number = dp[j]->part_number;
        new_p->num_parts = dp[j]->num_partblocks;
        new_p->size = dp[j]->size;
        new_p->scheme = dp[j]->dpb_queue.head->b->type;
        new_p->disk_parts = (DiskPart **) xmalloc(gndisks * sizeof(DiskPart *));
        memset(new_p->disk_parts, 0, gndisks * sizeof(DiskPart *));
        new_p->disk_parts[i] = dp[j];
        cp_list.enqueue(new_p);
        cp_list_len++;
      }
    }
  }
}


void
cplist_update()
{
  /* go through cplist and delete partitions that are not in the partition.config */
  CachePart *cp = cp_list.head;

  while (cp) {
    ConfigPart *config_part = config_partitions.cp_queue.head;
    for (; config_part; config_part = config_part->link.next) {
      if (config_part->number == cp->part_number) {
        int size_in_blocks = config_part->size << (20 - STORE_BLOCK_SHIFT);
        if ((cp->size <= size_in_blocks) && (cp->scheme == config_part->scheme)) {
          config_part->cachep = cp;
        } else {
          /* delete this partition from all the disks */
          int d_no;
          for (d_no = 0; d_no < gndisks; d_no++) {
            if (cp->disk_parts[d_no])
              cp->disk_parts[d_no]->disk->delete_partition(cp->part_number);
          }
          config_part = NULL;
        }
        break;
      }
    }

    if (!config_part) {
      // did not find a matching partition in the config file. 
      //Delete hte partition from the cache part list
      int d_no;
      for (d_no = 0; d_no < gndisks; d_no++) {
        if (cp->disk_parts[d_no])
          cp->disk_parts[d_no]->disk->delete_partition(cp->part_number);
      }
      CachePart *temp_cp = cp;
      cp = cp->link.next;
      cp_list.remove(temp_cp);
      cp_list_len--;
      delete temp_cp;
      continue;
    } else
      cp = cp->link.next;
  }
}

int
cplist_reconfigure()
{
  int i, j;
  int size;
  int partition_number;
  int size_in_blocks;

  gnpart = 0;
  if (config_partitions.num_partitions == 0) {
    /* only the http cache */
    CachePart *cp = NEW(new CachePart());
    cp->part_number = 0;
    cp->scheme = CACHE_HTTP_TYPE;
    cp->disk_parts = (DiskPart **) xmalloc(gndisks * sizeof(DiskPart *));
    memset(cp->disk_parts, 0, gndisks * sizeof(DiskPart *));
    cp_list.enqueue(cp);
    cp_list_len++;
    for (i = 0; i < gndisks; i++) {
      if (gdisks[i]->header->num_partitions != 1 || gdisks[i]->disk_parts[0]->part_number != 0) {
        /* The user had created several partitions before - clear the disk
           and create one partition for http */
        Note("Clearing Disk: %s", gdisks[i]->path);
        gdisks[i]->delete_all_partitions();
      }
      if (gdisks[i]->cleared) {
        inku64 free_space = gdisks[i]->free_space * STORE_BLOCK_SIZE;
        int parts = (free_space / MAX_PART_SIZE) + 1;
        for (int p = 0; p < parts; p++) {
          ink_off_t b = gdisks[i]->free_space / (parts - p);
          Debug("cache_hosting", "blocks = %d\n", b);
          DiskPartBlock *dpb = gdisks[i]->create_partition(0, b, CACHE_HTTP_TYPE);
          ink_assert(dpb && dpb->len == b);
        }
        ink_assert(gdisks[i]->free_space == 0);
      }

      ink_assert(gdisks[i]->header->num_partitions == 1);
      DiskPart **dp = gdisks[i]->disk_parts;
      gnpart += dp[0]->num_partblocks;
      cp->size += dp[0]->size;
      cp->num_parts += dp[0]->num_partblocks;
      cp->disk_parts[i] = dp[0];
    }

  } else {
    for (i = 0; i < gndisks; i++) {
      if (gdisks[i]->header->num_partitions == 1 && gdisks[i]->disk_parts[0]->part_number == 0) {
        /* The user had created several partitions before - clear the disk
           and create one partition for http */
        Note("Clearing Disk: %s", gdisks[i]->path);
        gdisks[i]->delete_all_partitions();
      }
    }

    /* change percentages in the config patitions to absolute value */
    ink64 tot_space_in_blks = 0;
    int blocks_per_part = PART_BLOCK_SIZE / STORE_BLOCK_SIZE;
    /* sum up the total space available on all the disks. 
       round down the space to 128 megabytes */
    for (i = 0; i < gndisks; i++)
      tot_space_in_blks += (gdisks[i]->num_usable_blocks / blocks_per_part) * blocks_per_part;

    double percent_remaining = 100.00;
    ConfigPart *config_part = config_partitions.cp_queue.head;
    for (; config_part; config_part = config_part->link.next) {
      if (config_part->in_percent) {
        if (config_part->percent > percent_remaining) {
          Warning("total partition sizes added up to more than 100%%!");
          Warning("no partitions created");
          return -1;
        }
        int space_in_blks = (int) (((double) (config_part->percent / percent_remaining)) * tot_space_in_blks);

        space_in_blks = space_in_blks >> (20 - STORE_BLOCK_SHIFT);
        /* round down to 128 megabyte multiple */
        space_in_blks = (space_in_blks >> 7) << 7;
        config_part->size = space_in_blks;
        tot_space_in_blks -= space_in_blks << (20 - STORE_BLOCK_SHIFT);
        percent_remaining -= (config_part->size < 128) ? 0 : config_part->percent;
      }
      if (config_part->size < 128) {
        Warning("the size of partition %d (%d) is less than the minimum required partition size",
                config_part->number, config_part->size, 128);
        Warning("partition %d is not created", config_part->number);
      }
      Debug("cache_hosting", "Partition: %d Size: %d", config_part->number, config_part->size);
    }
    cplist_update();
    /* go through partition config and grow and create partitiosn */

    config_part = config_partitions.cp_queue.head;

    for (; config_part; config_part = config_part->link.next) {

      size = config_part->size;
      if (size < 128)
        continue;

      partition_number = config_part->number;

      size_in_blocks = ((ink_off_t) size * 1024 * 1024) / STORE_BLOCK_SIZE;

      if (!config_part->cachep) {
        // we did not find a corresponding entry in cache part...creat one 

        CachePart *new_cp = NEW(new CachePart());
        new_cp->disk_parts = (DiskPart **) xmalloc(gndisks * sizeof(DiskPart *));
        memset(new_cp->disk_parts, 0, gndisks * sizeof(DiskPart *));
        if (create_partition(config_part->number, size_in_blocks, config_part->scheme, new_cp))
          return -1;
        cp_list.enqueue(new_cp);
        cp_list_len++;
        config_part->cachep = new_cp;
        gnpart += new_cp->num_parts;
        continue;
      }
//    else 
      CachePart *cp = config_part->cachep;
      ink_assert(cp->size <= size_in_blocks);
      if (cp->size == size_in_blocks) {
        gnpart += cp->num_parts;
        continue;
      }
      // else the size is greater...
      /* search the cp_list */

      int *sorted_part = new int[gndisks];
      for (i = 0; i < gndisks; i++) {
        sorted_part[i] = i;
      }
      for (i = 0; i < gndisks - 1; i++) {
        int smallest = sorted_part[i];
        int smallest_ndx = i;
        for (j = i + 1; j < gndisks; j++) {
          int curr = sorted_part[j];
          DiskPart *dpart = cp->disk_parts[curr];
          if (gdisks[curr]->cleared) {
            ink_assert(!dpart);
            // disks that are cleared should be filled first
            smallest = curr;
            smallest_ndx = j;
          } else if (!dpart && cp->disk_parts[smallest]) {

            smallest = curr;
            smallest_ndx = j;
          } else if (dpart && cp->disk_parts[smallest] && (dpart->size < cp->disk_parts[smallest]->size)) {
            smallest = curr;
            smallest_ndx = j;
          }
        }
        sorted_part[smallest_ndx] = sorted_part[i];
        sorted_part[i] = smallest;
      }

      int size_to_alloc = size_in_blocks - cp->size;
      int disk_full = 0;
      for (i = 0; (i < gndisks) && size_to_alloc; i++) {

        int disk_no = sorted_part[i];
        ink_assert(cp->disk_parts[sorted_part[gndisks - 1]]);
        int largest_part = cp->disk_parts[sorted_part[gndisks - 1]]->size;

        /* allocate storage on new disk. Find the difference
           between the biggest partition on any disk and 
           the partition on this disk and try to make 
           them equal */
        int size_diff = (cp->disk_parts[disk_no]) ? largest_part - cp->disk_parts[disk_no]->size : largest_part;
        size_diff = (size_diff < size_to_alloc) ? size_diff : size_to_alloc;
        /* if size_diff == 0, then then the disks have partitions of the
           same sizes, so we don't need to balance the disks */
        if (size_diff == 0)
          break;

        DiskPartBlock *dpb;
        do {
          dpb = gdisks[disk_no]->create_partition(partition_number, size_diff, cp->scheme);
          if (dpb) {
            if (!cp->disk_parts[disk_no]) {
              cp->disk_parts[disk_no] = gdisks[disk_no]->get_diskpart(partition_number);
            }
            size_diff -= dpb->len;
            cp->size += dpb->len;
            cp->num_parts++;
          } else
            break;
        } while ((size_diff > 0));

        if (!dpb)
          disk_full++;

        size_to_alloc = size_in_blocks - cp->size;

      }

      delete[]sorted_part;

      if (size_to_alloc) {
        if (create_partition(partition_number, size_to_alloc, cp->scheme, cp)) {
          return -1;
        }
      }
      gnpart += cp->num_parts;
    }
  }
  return 0;
}

int
create_partition(int partition_number, int size_in_blocks, int scheme, CachePart *cp)
{
  static int curr_part = 0;
  int to_create = size_in_blocks;
  int blocks_per_part = PART_BLOCK_SIZE >> STORE_BLOCK_SHIFT;
  int full_disks = 0;

  int *sp = new int[gndisks];
  memset(sp, 0, gndisks * sizeof(int));

  int i = curr_part;
  while (size_in_blocks > 0) {
    if (gdisks[i]->free_space >= (sp[i] + blocks_per_part)) {
      sp[i] += blocks_per_part;
      size_in_blocks -= blocks_per_part;
      full_disks = 0;
    } else {
      full_disks += 1;
      if (full_disks == gndisks) {
        char config_file[PATH_NAME_MAX];
        IOCORE_ReadConfigString(config_file, "proxy.config.cache.partition_filename", PATH_NAME_MAX);
        if (cp->size)
          Warning("not enough space to increase partition: [%d] to size: [%d]",
                  partition_number, (to_create + cp->size) >> (20 - STORE_BLOCK_SHIFT));
        else
          Warning("not enough space to create partition: [%d], size: [%d]",
                  partition_number, to_create >> (20 - STORE_BLOCK_SHIFT));

        Note("edit the %s file and restart traffic_server", config_file);
        delete[]sp;
        return -1;
      }
    }
    i = (i + 1) % gndisks;
  }
  cp->part_number = partition_number;
  cp->scheme = scheme;
  curr_part = i;
  for (i = 0; i < gndisks; i++) {
    if (sp[i] > 0) {
      while (sp[i] > 0) {
        DiskPartBlock *p = gdisks[i]->create_partition(partition_number,
                                                       sp[i], scheme);

        ink_assert(p && (p->len >= (unsigned int) blocks_per_part));
        sp[i] -= p->len;
        cp->num_parts++;
        cp->size += p->len;
      }
      if (!cp->disk_parts[i])
        cp->disk_parts[i] = gdisks[i]->get_diskpart(partition_number);
    }
  }
  delete[]sp;
  return 0;
}

void
rebuild_host_table(Cache *cache)
{
  build_part_hash_table(&cache->hosttable->gen_host_rec);
  if (cache->hosttable->m_numEntries != 0) {
    CacheHostMatcher *hm = cache->hosttable->getHostMatcher();
    CacheHostRecord *h_rec = hm->getDataArray();
    int h_rec_len = hm->getNumElements();
    int i;
    for (i = 0; i < h_rec_len; i++) {
      build_part_hash_table(&h_rec[i]);
    }
  }
}

// if generic_host_rec.parts == NULL, what do we do??? 
Part *
Cache::key_to_part(CacheKey *key, char *hostname, int host_len)
{
  inku32 h = (key->word(2) >> DIR_TAG_WIDTH) % PART_HASH_TABLE_SIZE;
  unsigned short *hash_table = hosttable->gen_host_rec.part_hash_table;
  CacheHostRecord *host_rec = &hosttable->gen_host_rec;
  if (hosttable->m_numEntries > 0 && host_len) {
    CacheHostResult res;
    hosttable->Match(hostname, host_len, &res);
    if (res.record) {
      unsigned short *host_hash_table = res.record->part_hash_table;
      if (host_hash_table) {
        char format_str[50];
        snprintf(format_str, sizeof(format_str), "Partition: %%xd for host: %%.%ds", host_len);
        Debug("cache_hosting", format_str, res.record, hostname);
        return res.record->parts[host_hash_table[h]];
      }
    }
  }
  if (hash_table) {
    char format_str[50];
    snprintf(format_str, sizeof(format_str), "Generic partition: %%xd for host: %%.%ds", host_len);
    Debug("cache_hosting", format_str, host_rec, hostname);
    return host_rec->parts[hash_table[h]];
  } else
    return host_rec->parts[0];
}

static void reg_int(const char *str, int stat, RecRawStatBlock *rsb, const char *prefix) {
  char stat_str[256];
  snprintf(stat_str, sizeof(stat_str), "%s.%s", prefix, str);
  RecRegisterRawStat(rsb, RECT_PROCESS,
                     stat_str, RECD_INT, RECP_NON_PERSISTENT, stat, RecRawStatSyncSum);
  DOCACHE_CLEAR_DYN_STAT(stat)
}
#define REG_INT(_str, _stat) reg_int(_str, (int)_stat, rsb, prefix)

// Register Stats
void
register_cache_stats(RecRawStatBlock *rsb, const char *prefix)
{
  char stat_str[256];

  REG_INT("bytes_used", cache_bytes_used_stat);
  REG_INT("bytes_total", cache_bytes_total_stat);
  snprintf(stat_str, sizeof(stat_str), "%s.%s", prefix, "ram_cache.total_bytes");
  RecRegisterRawStat(rsb, RECT_PROCESS,
                     stat_str, RECD_INT, RECP_NULL, (int) cache_ram_cache_bytes_total_stat, RecRawStatSyncSum);
  REG_INT("ram_cache.bytes_used", cache_ram_cache_bytes_stat);
  REG_INT("ram_cache.hits", cache_ram_cache_hits_stat);
  REG_INT("pread_count", cache_pread_count_stat);
  REG_INT("percent_full", cache_percent_full_stat);
  REG_INT("lookup.active", cache_lookup_active_stat);
  REG_INT("lookup.success", cache_lookup_success_stat);
  REG_INT("lookup.failure", cache_lookup_failure_stat);
  REG_INT("read.active", cache_read_active_stat);
  REG_INT("read.success", cache_read_success_stat);
  REG_INT("read.failure", cache_read_failure_stat);
  REG_INT("write.active", cache_write_active_stat);
  REG_INT("write.success", cache_write_success_stat);
  REG_INT("write.failure", cache_write_failure_stat);
  REG_INT("write.backlog.failure", cache_write_backlog_failure_stat);
  REG_INT("update.active", cache_update_active_stat);
  REG_INT("update.success", cache_update_success_stat);
  REG_INT("update.failure", cache_update_failure_stat);
  REG_INT("remove.active", cache_remove_active_stat);
  REG_INT("remove.success", cache_remove_success_stat);
  REG_INT("remove.failure", cache_remove_failure_stat);
  REG_INT("evacuate.active", cache_evacuate_active_stat);
  REG_INT("evacuate.success", cache_evacuate_success_stat);
  REG_INT("evacuate.failure", cache_evacuate_failure_stat);
  REG_INT("scan.active", cache_scan_active_stat);
  REG_INT("scan.success", cache_scan_success_stat);
  REG_INT("scan.failure", cache_scan_failure_stat);
  REG_INT("direntries.total", cache_direntries_total_stat);
  REG_INT("direntries.used", cache_direntries_used_stat);
  REG_INT("directory_collision", cache_directory_collision_count_stat);
  REG_INT("frags_per_doc.1", cache_single_fragment_document_count_stat);
  REG_INT("frags_per_doc.2", cache_two_fragment_document_count_stat);
  REG_INT("frags_per_doc.3+", cache_three_plus_plus_fragment_document_count_stat);
  REG_INT("read_busy.success", cache_read_busy_success_stat);
  REG_INT("read_busy.failure", cache_read_busy_failure_stat);
  REG_INT("write_bytes_stat", cache_write_bytes_stat);
  REG_INT("vector_marshals", cache_hdr_vector_marshal_stat);
  REG_INT("hdr_marshals", cache_hdr_marshal_stat);
  REG_INT("hdr_marshal_bytes", cache_hdr_marshal_bytes_stat);
  REG_INT("gc_bytes_evacuated", cache_gc_bytes_evacuated_stat);
  REG_INT("gc_frags_evacuated", cache_gc_frags_evacuated_stat);
}


void
ink_cache_init(ModuleVersion v)
{
  struct stat s;
  int ierr;
  ink_release_assert(!checkModuleVersion(v, CACHE_MODULE_VERSION));

  cache_rsb = RecAllocateRawStatBlock((int) cache_stat_count);


  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.min_average_object_size", 8000, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigLLong(RECT_CONFIG, "proxy.config.cache.ram_cache.size", -1, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigLLong(cache_config_ram_cache_size, "proxy.config.cache.ram_cache.size");
  Debug("cache_init", "proxy.config.cache.ram_cache.size = %lld = %lldMb",
        cache_config_ram_cache_size, cache_config_ram_cache_size / (1024 * 1024));

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.limits.http.max_alts", 3, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_http_max_alts, "proxy.config.cache.limits.http.max_alts");
  Debug("cache_init", "proxy.config.cache.limits.http.max_alts = %d", cache_config_http_max_alts);

  IOCORE_RegisterConfigLLong(RECT_CONFIG,
                             "proxy.config.cache.ram_cache_cutoff", 1048576, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigLLong(cache_config_ram_cache_cutoff, "proxy.config.cache.ram_cache_cutoff");
  Debug("cache_init", "cache_config_ram_cache_cutoff = %lld = %lldMb",
        cache_config_ram_cache_cutoff, cache_config_ram_cache_cutoff / (1024 * 1024));

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.ram_cache_mixt_cutoff", 1048576, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInteger(cache_config_ram_cache_mixt_cutoff, "proxy.config.cache.ram_cache_mixt_cutoff");
  Debug("cache_init", "proxy.config.cache.ram_cache_mixt_cutoff = %lld = %lldMb",
        cache_config_ram_cache_mixt_cutoff, cache_config_ram_cache_mixt_cutoff / (1024 * 1024));

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.permit.pinning", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_permit_pinning, "proxy.config.cache.permit.pinning");
  Debug("cache_init", "proxy.config.cache.permit.pinning = %d", cache_config_permit_pinning);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.dir.sync_frequency", 60, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_dir_sync_frequency, "proxy.config.cache.dir.sync_frequency");
  Debug("cache_init", "proxy.config.cache.dir.sync_frequency = %d", cache_config_dir_sync_frequency);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.vary_on_user_agent", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_vary_on_user_agent, "proxy.config.cache.vary_on_user_agent");
  Debug("cache_init", "proxy.config.cache.vary_on_user_agent = %d", cache_config_vary_on_user_agent);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.select_alternate", 1, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_select_alternate, "proxy.config.cache.select_alternate");
  Debug("cache_init", "proxy.config.cache.select_alternate = %d", cache_config_select_alternate);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.max_doc_size", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_max_doc_size, "proxy.config.cache.max_doc_size");
  Debug("cache_init", "proxy.config.cache.max_doc_size = %d = %dMb",
        cache_config_max_doc_size, cache_config_max_doc_size / (1024 * 1024));

  IOCORE_RegisterConfigString(RECT_CONFIG, "proxy.config.config_dir", SYSCONFDIR, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_ReadConfigString(cache_system_config_directory, "proxy.config.config_dir", PATH_NAME_MAX);
  Debug("cache_init", "proxy.config.config_dir = \"%s\"", cache_system_config_directory);
  if ((ierr = stat(cache_system_config_directory, &s)) < 0) {
    ink_strncpy(cache_system_config_directory,system_config_directory,sizeof(cache_system_config_directory)); 
    if ((ierr = stat(cache_system_config_directory, &s)) < 0) {
      // Try 'system_root_dir/etc/trafficserver' directory
      snprintf(cache_system_config_directory, sizeof(cache_system_config_directory), 
               "%s%s%s%s%s",system_root_dir, DIR_SEP,"etc",DIR_SEP,"trafficserver");
      if ((ierr = stat(cache_system_config_directory, &s)) < 0) {
        fprintf(stderr,"unable to stat() config dir '%s': %d %d, %s\n", 
                cache_system_config_directory, ierr, errno, strerror(errno));
        fprintf(stderr, "please set config path via 'proxy.config.config_dir' \n");
        _exit(1);
      }
    }
  }
#ifdef HIT_EVACUATE

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.hit_evacuate_percent", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_hit_evacuate_percent, "proxy.config.cache.hit_evacuate_percent");
  Debug("cache_init", "proxy.config.cache.hit_evacuate_percent = %d", cache_config_hit_evacuate_percent);

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.hit_evacuate_size_limit", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_hit_evacuate_size_limit, "proxy.config.cache.hit_evacuate_size_limit");
  Debug("cache_init", "proxy.config.cache.hit_evacuate_size_limit = %d", cache_config_hit_evacuate_size_limit);
#endif
#ifdef HTTP_CACHE
  extern int url_hash_method;
  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.url_hash_method", 1, RECU_RESTART_TS, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(url_hash_method, "proxy.config.cache.url_hash_method");
  Debug("cache_init", "proxy.config.cache.url_hash_method = %d", url_hash_method);
#endif

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.max_disk_errors", 5, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_max_disk_errors, "proxy.config.cache.max_disk_errors");
  Debug("cache_init", "proxy.config.cache.max_disk_errors = %d", cache_config_max_disk_errors);

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.agg_write_backlog", 5242880, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_agg_write_backlog, "proxy.config.cache.agg_write_backlog");
  Debug("cache_init", "proxy.config.cache.agg_write_backlog = %d", cache_config_agg_write_backlog);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.enable_checksum", 1, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_enable_checksum, "proxy.config.cache.enable_checksum");
  Debug("cache_init", "proxy.config.cache.enable_checksum = %d", cache_config_enable_checksum);

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.alt_rewrite_max_size", 4096, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_alt_rewrite_max_size, "proxy.config.cache.alt_rewrite_max_size");
  Debug("cache_init", "proxy.config.cache.alt_rewrite_max_size = %d", cache_config_alt_rewrite_max_size);

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cache.enable_read_while_writer", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(cache_config_read_while_writer, "proxy.config.cache.enable_read_while_writer");
  Debug("cache_init", "proxy.config.cache.enable_read_while_writer = %d", cache_config_read_while_writer);

  IOCORE_RegisterConfigUpdateFunc("proxy.config.cache.enable_read_while_writer", update_cache_config, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.cache.partition_filename",
                              "partition.config", RECU_RESTART_TS, RECC_NULL, NULL);


  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.cache.hosting_filename", "hosting.config", RECU_DYNAMIC, RECC_NULL, NULL);


  register_cache_stats(cache_rsb, "proxy.process.cache");
  IOCORE_RegisterStatUpdateFunc("proxy.process.cache.bytes_used",
                                cache_rsb, (int) cache_bytes_used_stat, cache_stats_bytes_used_cb, NULL);

  char *err = NULL;
  if ((err = theCacheStore.read_config())) {
    printf("%s  failed\n", err);
    exit(1);
  }
  if (theCacheStore.n_disks == 0) {
    char p[PATH_NAME_MAX];
    snprintf(p, sizeof(p), "%s/", cache_system_config_directory);
    IOCORE_ReadConfigString(p + strlen(p), "proxy.config.cache.storage_filename", PATH_NAME_MAX - strlen(p) - 1);
    if (p[strlen(p) - 1] == '/' || p[strlen(p) - 1] == '\\') {
      strncat(p, "storage.config", (sizeof(p) - strlen(p) - 1));
    }
    Warning("no cache disks specified in %s: cache disabled\n", p);
    //exit(1);
  }
}


//----------------------------------------------------------------------------
Action *
CacheProcessor::open_read(Continuation *cont, URL *url, CacheHTTPHdr *request,
                          CacheLookupHttpConfig *params, time_t pin_in_cache, CacheFragType type)
{
#ifdef CLUSTER_CACHE
  if (cache_clustering_enabled > 0) {
    return open_read_internal(CACHE_OPEN_READ_LONG, cont, (MIOBuffer *) 0,
                              url, request, params, (CacheKey *) 0, pin_in_cache, type, (char *) 0, 0);
  }
#endif
  if (cache_global_hooks != NULL && cache_global_hooks->hooks_set > 0) {
    Debug("cache_plugin", "[CacheProcessor::open_read] Cache hooks are set");
    APIHook *cache_lookup = cache_global_hooks->get(INK_CACHE_PLUGIN_HOOK);

    if (cache_lookup != NULL) {
      HttpCacheSM *sm = (HttpCacheSM *) cont;
      if (sm != NULL) {
        if (sm->master_sm && sm->master_sm->t_state.cache_vc) {
          Debug("cache_plugin", "[CacheProcessor::open_read] Freeing existing cache_vc");
          sm->master_sm->t_state.cache_vc->free();
          sm->master_sm->t_state.cache_vc = NULL;
        }
        NewCacheVC *vc = NewCacheVC::alloc(cont, url, sm);
        vc->setConfigParams(params);
        vc->set_cache_http_hdr(request);
        if (sm->master_sm) {
          sm->master_sm->t_state.cache_vc = vc;
        }
        //vc->setCtrlInPlugin(true);
        int rval = cache_lookup->invoke(INK_EVENT_CACHE_LOOKUP, (void *) vc);
        if (rval == INK_SUCCESS) {
          return ACTION_RESULT_DONE;
        } else {
          abort();
        }
      } else {
        Error("[CacheProcessor::open_read] cache sm is NULL");
      }
    }
  }

  return caches[type]->open_read(cont, url, request, params, type);
}


//----------------------------------------------------------------------------
Action *
CacheProcessor::open_write(Continuation *cont, int expected_size, URL *url,
                           CacheHTTPHdr *request, CacheHTTPInfo *old_info, time_t pin_in_cache, CacheFragType type)
{
#ifdef CLUSTER_CACHE
  if (cache_clustering_enabled > 0) {
    INK_MD5 url_md5;
    Cache::generate_key(&url_md5, url, request);
    ClusterMachine *m = cluster_machine_at_depth(cache_hash(url_md5));

    if (m) {
      // Do remote open_write()
      INK_MD5 url_only_md5;
      Cache::generate_key(&url_only_md5, url, 0);
      return Cluster_write(cont, expected_size, (MIOBuffer *) 0, m,
                           &url_only_md5, type,
                           false, pin_in_cache, CACHE_OPEN_WRITE_LONG,
                           (CacheKey *) 0, url, request, old_info, (char *) 0, 0);
    }
  }
#endif
  // cache plugin
  if (cache_global_hooks != NULL && cache_global_hooks->hooks_set > 0) {
    Debug("cache_plugin", "[CacheProcessor::open_write] Cache hooks are set, old_info=%lX", (long) old_info);

    HttpCacheSM *sm = (HttpCacheSM *) cont;
    if (sm->master_sm && sm->master_sm->t_state.cache_vc) {
      // use NewCacheVC from lookup
      NewCacheVC *vc = sm->master_sm->t_state.cache_vc;
      vc->setWriteVC(old_info);
      //vc->setCtrlInPlugin(true);
      // since we are reusing the read vc, set it to NULL to prevent double io_close
      sm->cache_read_vc = NULL;
      sm->handleEvent(CACHE_EVENT_OPEN_WRITE, (void *) vc);
      return ACTION_RESULT_DONE;
    } else {
      DDebug("cache_plugin", "[CacheProcessor::open_write] Error: NewCacheVC not set");
      sm->handleEvent(CACHE_EVENT_OPEN_WRITE_FAILED, (void *) -ECACHE_WRITE_FAIL);
      return ACTION_RESULT_DONE;
    }
  }
  return caches[type]->open_write(cont, url, request, old_info, pin_in_cache, type);
}

//----------------------------------------------------------------------------
Action *
CacheProcessor::remove(Continuation *cont, URL *url, CacheFragType frag_type)
{
#ifdef CLUSTER_CACHE
  if (cache_clustering_enabled > 0) {
  }
#endif
  if (cache_global_hooks != NULL && cache_global_hooks->hooks_set > 0) {
    DDebug("cache_plugin", "[CacheProcessor::remove] Cache hooks are set");
    APIHook *cache_lookup = cache_global_hooks->get(INK_CACHE_PLUGIN_HOOK);
    if (cache_lookup != NULL) {
      NewCacheVC *vc = NewCacheVC::alloc(cont, url, NULL);
      int rval = cache_lookup->invoke(INK_EVENT_CACHE_DELETE, (void *) vc);
      if (vc) {
        vc->free();
      }
      if (rval == INK_SUCCESS) {
        return ACTION_RESULT_DONE;
      } else {
        abort();
      }
    }
  }
  return caches[frag_type]->remove(cont, url, frag_type);
}
