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

// Cache Inspector and State Pages
#include "P_CacheTest.h"
#include "StatPages.h"

#include "tscore/I_Layout.h"

#include "HttpTransactCache.h"
#include "HttpSM.h"
#include "HttpCacheSM.h"
#include "InkAPIInternal.h"
#include "P_CacheBC.h"

#include "tscore/hugepages.h"

#include <atomic>

constexpr ts::VersionNumber CACHE_DB_VERSION(CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION);

// Compilation Options
#define USELESS_REENABLES // allow them for now
// #define VERIFY_JTEST_DATA

static size_t DEFAULT_RAM_CACHE_MULTIPLIER = 10; // I.e. 10x 1MB per 1GB of disk.

// This is the oldest version number that is still usable.
static short int const CACHE_DB_MAJOR_VERSION_COMPATIBLE = 21;

#define DOCACHE_CLEAR_DYN_STAT(x)  \
  do {                             \
    RecSetRawStatSum(rsb, x, 0);   \
    RecSetRawStatCount(rsb, x, 0); \
  } while (0);

// Configuration

int64_t cache_config_ram_cache_size            = AUTO_SIZE_RAM_CACHE;
int cache_config_ram_cache_algorithm           = 1;
int cache_config_ram_cache_compress            = 0;
int cache_config_ram_cache_compress_percent    = 90;
int cache_config_ram_cache_use_seen_filter     = 1;
int cache_config_http_max_alts                 = 3;
int cache_config_dir_sync_frequency            = 60;
int cache_config_permit_pinning                = 0;
int cache_config_select_alternate              = 1;
int cache_config_max_doc_size                  = 0;
int cache_config_min_average_object_size       = ESTIMATED_OBJECT_SIZE;
int64_t cache_config_ram_cache_cutoff          = AGG_SIZE;
int cache_config_max_disk_errors               = 5;
int cache_config_hit_evacuate_percent          = 10;
int cache_config_hit_evacuate_size_limit       = 0;
int cache_config_force_sector_size             = 0;
int cache_config_target_fragment_size          = DEFAULT_TARGET_FRAGMENT_SIZE;
int cache_config_agg_write_backlog             = AGG_SIZE * 2;
int cache_config_enable_checksum               = 0;
int cache_config_alt_rewrite_max_size          = 4096;
int cache_config_read_while_writer             = 0;
int cache_config_mutex_retry_delay             = 2;
int cache_read_while_writer_retry_delay        = 50;
int cache_config_read_while_writer_max_retries = 10;
static int enable_cache_empty_http_doc         = 0;
/// Fix up a specific known problem with the 4.2.0 release.
/// Not used for stripes with a cache version later than 4.2.0.
int cache_config_compatibility_4_2_0_fixup = 1;

// Globals

RecRawStatBlock *cache_rsb          = nullptr;
Cache *theCache                     = nullptr;
CacheDisk **gdisks                  = nullptr;
int gndisks                         = 0;
std::atomic<int> initialize_disk    = 0;
Cache *caches[NUM_CACHE_FRAG_TYPES] = {nullptr};
CacheSync *cacheDirSync             = nullptr;
Store theCacheStore;
int CacheProcessor::initialized          = CACHE_INITIALIZING;
uint32_t CacheProcessor::cache_ready     = 0;
int CacheProcessor::start_done           = 0;
bool CacheProcessor::clear               = false;
bool CacheProcessor::fix                 = false;
bool CacheProcessor::check               = false;
int CacheProcessor::start_internal_flags = 0;
int CacheProcessor::auto_clear_flag      = 0;
CacheProcessor cacheProcessor;
Vol **gvol             = nullptr;
std::atomic<int> gnvol = 0;
ClassAllocator<CacheVC> cacheVConnectionAllocator("cacheVConnection");
ClassAllocator<EvacuationBlock> evacuationBlockAllocator("evacuationBlock");
ClassAllocator<CacheRemoveCont> cacheRemoveContAllocator("cacheRemoveCont");
ClassAllocator<EvacuationKey> evacuationKeyAllocator("evacuationKey");
int CacheVC::size_to_init = -1;
CacheKey zero_key;

struct VolInitInfo {
  off_t recover_pos;
  AIOCallbackInternal vol_aio[4];
  char *vol_h_f;

  VolInitInfo()
  {
    recover_pos = 0;
    vol_h_f     = (char *)ats_memalign(ats_pagesize(), 4 * STORE_BLOCK_SIZE);
    memset(vol_h_f, 0, 4 * STORE_BLOCK_SIZE);
  }

  ~VolInitInfo()
  {
    for (auto &i : vol_aio) {
      i.action = nullptr;
      i.mutex.clear();
    }
    free(vol_h_f);
  }
};

#if AIO_MODE == AIO_MODE_NATIVE
struct VolInit : public Continuation {
  Vol *vol;
  char *path;
  off_t blocks;
  int64_t offset;
  bool vol_clear;

  int
  mainEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    vol->init(path, blocks, offset, vol_clear);
    mutex.clear();
    delete this;
    return EVENT_DONE;
  }

  VolInit(Vol *v, char *p, off_t b, int64_t o, bool c) : Continuation(v->mutex), vol(v), path(p), blocks(b), offset(o), vol_clear(c)
  {
    SET_HANDLER(&VolInit::mainEvent);
  }
};

struct DiskInit : public Continuation {
  CacheDisk *disk;
  char *s;
  off_t blocks;
  off_t askip;
  int ahw_sector_size;
  int fildes;
  bool clear;

  int
  mainEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    disk->open(s, blocks, askip, ahw_sector_size, fildes, clear);
    ats_free(s);
    mutex.clear();
    delete this;
    return EVENT_DONE;
  }

  DiskInit(CacheDisk *d, char *str, off_t b, off_t skip, int sector, int f, bool c)
    : Continuation(d->mutex), disk(d), s(ats_strdup(str)), blocks(b), askip(skip), ahw_sector_size(sector), fildes(f), clear(c)
  {
    SET_HANDLER(&DiskInit::mainEvent);
  }
};
#endif
void cplist_init();
static void cplist_update();
int cplist_reconfigure();
static int create_volume(int volume_number, off_t size_in_blocks, int scheme, CacheVol *cp);
static void rebuild_host_table(Cache *cache);
void register_cache_stats(RecRawStatBlock *rsb, const char *prefix);

// Global list of the volumes created
Queue<CacheVol> cp_list;
int cp_list_len = 0;
ConfigVolumes config_volumes;

#if TS_HAS_TESTS
void
force_link_CacheTestCaller()
{
  force_link_CacheTest();
}
#endif

int64_t
cache_bytes_used(int volume)
{
  uint64_t used = 0;

  for (int i = 0; i < gnvol; i++) {
    if (!DISK_BAD(gvol[i]->disk) && (volume == -1 || gvol[i]->cache_vol->vol_number == volume)) {
      if (!gvol[i]->header->cycle) {
        used += gvol[i]->header->write_pos - gvol[i]->start;
      } else {
        used += gvol[i]->len - gvol[i]->dirlen() - EVACUATION_SIZE;
      }
    }
  }

  return used;
}

int
cache_stats_bytes_used_cb(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  int volume = -1;
  char *p;

  // Well, there's no way to pass along the volume ID, so extracting it from the stat name.
  p = strstr((char *)name, "volume_");
  if (p != nullptr) {
    // I'm counting on the compiler to optimize out strlen("volume_").
    volume = strtol(p + strlen("volume_"), nullptr, 10);
  }

  if (cacheProcessor.initialized == CACHE_INITIALIZED) {
    int64_t used, total = 0;
    float percent_full;

    used = cache_bytes_used(volume);
    RecSetGlobalRawStatSum(rsb, id, used);
    RecRawStatSyncSum(name, data_type, data, rsb, id);
    RecGetGlobalRawStatSum(rsb, (int)cache_bytes_total_stat, &total);
    percent_full = (float)used / (float)total * 100;
    // The perent_full float below gets rounded down
    RecSetGlobalRawStatSum(rsb, (int)cache_percent_full_stat, (int64_t)percent_full);
  }

  return 1;
}

static int
validate_rww(int new_value)
{
  if (new_value) {
    float http_bg_fill;

    REC_ReadConfigFloat(http_bg_fill, "proxy.config.http.background_fill_completed_threshold");
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
    return new_value;
  }
  return 0;
}

static int
update_cache_config(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
                    void * /* cookie ATS_UNUSED */)
{
  int new_value                  = validate_rww(data.rec_int);
  cache_config_read_while_writer = new_value;

  return 0;
}

CacheVC::CacheVC()
{
  size_to_init = sizeof(CacheVC) - (size_t) & ((CacheVC *)nullptr)->vio;
  memset((void *)&vio, 0, size_to_init);
}

HTTPInfo::FragOffset *
CacheVC::get_frag_table()
{
  ink_assert(alternate.valid());
  return alternate.valid() ? alternate.get_frag_table() : nullptr;
}

VIO *
CacheVC::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *abuf)
{
  ink_assert(vio.op == VIO::READ);
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone     = 0;
  vio.nbytes    = nbytes;
  vio.vc_server = this;
#ifdef DEBUG
  ink_assert(!c || c->mutex->thread_holding);
#endif
  if (c && !trigger && !recursive) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }
  return &vio;
}

VIO *
CacheVC::do_io_pread(Continuation *c, int64_t nbytes, MIOBuffer *abuf, int64_t offset)
{
  ink_assert(vio.op == VIO::READ);
  vio.buffer.writer_for(abuf);
  vio.set_continuation(c);
  vio.ndone     = 0;
  vio.nbytes    = nbytes;
  vio.vc_server = this;
  seek_to       = offset;
#ifdef DEBUG
  ink_assert(c->mutex->thread_holding);
#endif
  if (!trigger && !recursive) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }
  return &vio;
}

VIO *
CacheVC::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuf, bool owner)
{
  ink_assert(vio.op == VIO::WRITE);
  ink_assert(!owner);
  vio.buffer.reader_for(abuf);
  vio.set_continuation(c);
  vio.ndone     = 0;
  vio.nbytes    = nbytes;
  vio.vc_server = this;
#ifdef DEBUG
  ink_assert(!c || c->mutex->thread_holding);
#endif
  if (c && !trigger && !recursive) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }
  return &vio;
}

void
CacheVC::do_io_close(int alerrno)
{
  ink_assert(mutex->thread_holding == this_ethread());
  int previous_closed = closed;
  closed              = (alerrno == -1) ? 1 : -1; // Stupid default arguments
  DDebug("cache_close", "do_io_close %p %d %d", this, alerrno, closed);
  if (!previous_closed && !recursive) {
    die();
  }
}

void
CacheVC::reenable(VIO *avio)
{
  DDebug("cache_reenable", "reenable %p", this);
  (void)avio;
#ifdef DEBUG
  ink_assert(avio->mutex->thread_holding);
#endif
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
  DDebug("cache_reenable", "reenable_re %p", this);
  (void)avio;
#ifdef DEBUG
  ink_assert(avio->mutex->thread_holding);
#endif
  if (!trigger) {
    if (!is_io_in_progress() && !recursive) {
      handleEvent(EVENT_NONE, (void *)nullptr);
    } else {
      trigger = avio->mutex->thread_holding->schedule_imm_local(this);
    }
  }
}

bool
CacheVC::get_data(int i, void *data)
{
  switch (i) {
  case CACHE_DATA_HTTP_INFO:
    *((CacheHTTPInfo **)data) = &alternate;
    return true;
  case CACHE_DATA_RAM_CACHE_HIT_FLAG:
    *((int *)data) = !f.not_from_ram_cache;
    return true;
  default:
    break;
  }
  return false;
}

int64_t
CacheVC::get_object_size()
{
  return ((CacheVC *)this)->doc_len;
}

bool
CacheVC::set_data(int /* i ATS_UNUSED */, void * /* data */)
{
  ink_assert(!"CacheVC::set_data should not be called!");
  return true;
}

void
CacheVC::get_http_info(CacheHTTPInfo **ainfo)
{
  *ainfo = &((CacheVC *)this)->alternate;
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
  if (enable_cache_empty_http_doc) {
    MIMEField *field = ainfo->m_alt->m_response_hdr.field_find(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
    if (field && !field->value_get_int64()) {
      f.allow_empty_doc = 1;
    } else {
      f.allow_empty_doc = 0;
    }
  } else {
    f.allow_empty_doc = 0;
  }
  alternate.copy_shallow(ainfo);
  ainfo->clear();
}

bool
CacheVC::set_pin_in_cache(time_t time_pin)
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

bool
CacheVC::set_disk_io_priority(int priority)
{
  ink_assert(priority >= AIO_LOWEST_PRIORITY);
  io.aiocb.aio_reqprio = priority;
  return true;
}

time_t
CacheVC::get_pin_in_cache()
{
  return pin_in_cache;
}

int
CacheVC::get_disk_io_priority()
{
  return io.aiocb.aio_reqprio;
}

int
Vol::begin_read(CacheVC *cont)
{
  ink_assert(cont->mutex->thread_holding == this_ethread());
  ink_assert(mutex->thread_holding == this_ethread());
#ifdef CACHE_STAT_PAGES
  ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
  stat_cache_vcs.enqueue(cont, cont->stat_link);
#endif
  // no need for evacuation as the entire document is already in memory
  if (cont->f.single_fragment) {
    return 0;
  }
  int i = dir_evac_bucket(&cont->earliest_dir);
  EvacuationBlock *b;
  for (b = evacuate[i].head; b; b = b->link.next) {
    if (dir_offset(&b->dir) != dir_offset(&cont->earliest_dir)) {
      continue;
    }
    if (b->readers) {
      b->readers = b->readers + 1;
    }
    return 0;
  }
  // we don't actually need to preserve this block as it is already in
  // memory, but this is easier, and evacuations are rare
  EThread *t        = cont->mutex->thread_holding;
  b                 = new_EvacuationBlock(t);
  b->readers        = 1;
  b->dir            = cont->earliest_dir;
  b->evac_frags.key = cont->earliest_key;
  evacuate[i].push(b);
  return 1;
}

int
Vol::close_read(CacheVC *cont)
{
  EThread *t = cont->mutex->thread_holding;
  ink_assert(t == this_ethread());
  ink_assert(t == mutex->thread_holding);
  if (dir_is_empty(&cont->earliest_dir)) {
    return 1;
  }
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
CacheProcessor::start(int, size_t)
{
  return start_internal(0);
}

static const int DEFAULT_CACHE_OPTIONS = (O_RDWR);

int
CacheProcessor::start_internal(int flags)
{
  ink_assert((int)TS_EVENT_CACHE_OPEN_READ == (int)CACHE_EVENT_OPEN_READ);
  ink_assert((int)TS_EVENT_CACHE_OPEN_READ_FAILED == (int)CACHE_EVENT_OPEN_READ_FAILED);
  ink_assert((int)TS_EVENT_CACHE_OPEN_WRITE == (int)CACHE_EVENT_OPEN_WRITE);
  ink_assert((int)TS_EVENT_CACHE_OPEN_WRITE_FAILED == (int)CACHE_EVENT_OPEN_WRITE_FAILED);
  ink_assert((int)TS_EVENT_CACHE_REMOVE == (int)CACHE_EVENT_REMOVE);
  ink_assert((int)TS_EVENT_CACHE_REMOVE_FAILED == (int)CACHE_EVENT_REMOVE_FAILED);
  ink_assert((int)TS_EVENT_CACHE_SCAN == (int)CACHE_EVENT_SCAN);
  ink_assert((int)TS_EVENT_CACHE_SCAN_FAILED == (int)CACHE_EVENT_SCAN_FAILED);
  ink_assert((int)TS_EVENT_CACHE_SCAN_OBJECT == (int)CACHE_EVENT_SCAN_OBJECT);
  ink_assert((int)TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED == (int)CACHE_EVENT_SCAN_OPERATION_BLOCKED);
  ink_assert((int)TS_EVENT_CACHE_SCAN_OPERATION_FAILED == (int)CACHE_EVENT_SCAN_OPERATION_FAILED);
  ink_assert((int)TS_EVENT_CACHE_SCAN_DONE == (int)CACHE_EVENT_SCAN_DONE);

#if AIO_MODE == AIO_MODE_NATIVE
  int etype            = ET_NET;
  int n_netthreads     = eventProcessor.n_threads_for_type[etype];
  EThread **netthreads = eventProcessor.eventthread[etype];
  for (int i = 0; i < n_netthreads; ++i) {
    netthreads[i]->diskHandler = new DiskHandler();
    netthreads[i]->schedule_imm(netthreads[i]->diskHandler);
  }
#endif

  start_internal_flags = flags;
  clear                = !!(flags & PROCESSOR_RECONFIGURE) || auto_clear_flag;
  fix                  = !!(flags & PROCESSOR_FIX);
  check                = (flags & PROCESSOR_CHECK) != 0;
  start_done           = 0;
  Span *sd;

  /* read the config file and create the data structures corresponding
     to the file */
  gndisks = theCacheStore.n_disks;
  gdisks  = (CacheDisk **)ats_malloc(gndisks * sizeof(CacheDisk *));

  gndisks = 0;
  ink_aio_set_callback(new AIO_Callback_handler());

  config_volumes.read_config_file();

  /*
   create CacheDisk objects for each span in the configuration file and store in gdisks
   */
  for (unsigned i = 0; i < theCacheStore.n_disks; i++) {
    sd = theCacheStore.disk[i];
    char path[PATH_NAME_MAX];
    int opts = DEFAULT_CACHE_OPTIONS;

    ink_strlcpy(path, sd->pathname, sizeof(path));
    if (!sd->file_pathname) {
      ink_strlcat(path, "/cache.db", sizeof(path));
      opts |= O_CREAT;
    }

#ifdef O_DIRECT
    opts |= O_DIRECT;
#endif
#ifdef O_DSYNC
    opts |= O_DSYNC;
#endif
    if (check) {
      opts &= ~O_CREAT;
      opts |= O_RDONLY;
    }

    int fd         = open(path, opts, 0644);
    int64_t blocks = sd->blocks;

    if (fd < 0 && (opts & O_CREAT)) { // Try without O_DIRECT if this is a file on filesystem, e.g. tmpfs.
      fd = open(path, DEFAULT_CACHE_OPTIONS | O_CREAT, 0644);
    }

    if (fd >= 0) {
      bool diskok = true;
      if (!sd->file_pathname) {
        if (!check) {
          if (ftruncate(fd, blocks * STORE_BLOCK_SIZE) < 0) {
            Warning("unable to truncate cache file '%s' to %" PRId64 " blocks", path, blocks);
            diskok = false;
          }
        } else { // read-only mode checks
          struct stat sbuf;
          if (-1 == fstat(fd, &sbuf)) {
            fprintf(stderr, "Failed to stat cache file for directory %s\n", path);
            diskok = false;
          } else if (blocks != sbuf.st_size / STORE_BLOCK_SIZE) {
            fprintf(stderr, "Cache file for directory %s is %" PRId64 " bytes, expected %" PRId64 "\n", path, sbuf.st_size,
                    blocks * static_cast<int64_t>(STORE_BLOCK_SIZE));
            diskok = false;
          }
        }
      }
      if (diskok) {
        int sector_size = sd->hw_sector_size;

        gdisks[gndisks] = new CacheDisk();
        if (check) {
          gdisks[gndisks]->read_only_p = true;
        }
        gdisks[gndisks]->forced_volume_num = sd->forced_volume_num;
        if (sd->hash_base_string) {
          gdisks[gndisks]->hash_base_string = ats_strdup(sd->hash_base_string);
        }

        if (sector_size < cache_config_force_sector_size) {
          sector_size = cache_config_force_sector_size;
        }

        // It's actually common that the hardware I/O size is larger than the store block size as
        // storage systems increasingly want larger I/Os. For example, on OS X, the filesystem block
        // size is always reported as 1MB.
        if (sd->hw_sector_size <= 0 || sector_size > STORE_BLOCK_SIZE) {
          Note("resetting hardware sector size from %d to %d", sector_size, STORE_BLOCK_SIZE);
          sector_size = STORE_BLOCK_SIZE;
        }

        off_t skip = ROUND_TO_STORE_BLOCK((sd->offset < START_POS ? START_POS + sd->alignment : sd->offset));
        blocks     = blocks - (skip >> STORE_BLOCK_SHIFT);
#if AIO_MODE == AIO_MODE_NATIVE
        eventProcessor.schedule_imm(new DiskInit(gdisks[gndisks], path, blocks, skip, sector_size, fd, clear));
#else
        gdisks[gndisks]->open(path, blocks, skip, sector_size, fd, clear);
#endif

        Debug("cache_hosting", "Disk: %d:%s, blocks: %" PRId64 "", gndisks, path, blocks);
        fd = -1;
        gndisks++;
      }
    } else {
      if (errno == EINVAL) {
        Warning("cache unable to open '%s': It must be placed on a file system that supports direct I/O.", path);
      } else {
        Warning("cache unable to open '%s': %s", path, strerror(errno));
      }
    }
    if (fd >= 0) {
      close(fd);
    }
  }

  start_done = 1;

  if (gndisks == 0) {
    CacheProcessor::initialized = CACHE_INIT_FAILED;
    // Have to do this here because no IO events were scheduled and so @c diskInitialized() won't be called.
    if (cb_after_init) {
      cb_after_init();
    }

    if (this->waitForCache() > 1) {
      Fatal("Cache initialization failed - no disks available but cache required");
    } else {
      Warning("unable to open cache disk(s): Cache Disabled\n");
      return -1; // pointless, AFAICT this is ignored.
    }
  } else if (this->waitForCache() == 3 && static_cast<unsigned int>(gndisks) < theCacheStore.n_disks_in_config) {
    CacheProcessor::initialized = CACHE_INIT_FAILED;
    if (cb_after_init) {
      cb_after_init();
    }
    Fatal("Cache initialization failed - only %d out of %d disks were valid and all were required.", gndisks,
          theCacheStore.n_disks_in_config);
  }

  return 0;
}

void
CacheProcessor::diskInitialized()
{
  int n_init    = initialize_disk++;
  int bad_disks = 0;
  int res       = 0;
  int i;

  // Wait for all the cache disks are initialized
  if (n_init != gndisks - 1) {
    return;
  }

  // Check and remove bad disks from gdisks[]
  for (i = 0; i < gndisks; i++) {
    if (DISK_BAD(gdisks[i])) {
      delete gdisks[i];
      gdisks[i] = nullptr;
      bad_disks++;
    } else if (bad_disks > 0) {
      gdisks[i - bad_disks] = gdisks[i];
      gdisks[i]             = nullptr;
    }
  }
  if (bad_disks > 0) {
    // Update the number of available cache disks
    gndisks -= bad_disks;
    // Check if this is a fatal error
    if (this->waitForCache() == 3 || (0 == gndisks && this->waitForCache() == 2)) {
      // This could be passed off to @c cacheInitialized (as with volume config problems) but I think
      // the more specific error message here is worth the extra code.
      CacheProcessor::initialized = CACHE_INIT_FAILED;
      if (cb_after_init) {
        cb_after_init();
      }
      Fatal("Cache initialization failed - only %d of %d disks were available.", gndisks, theCacheStore.n_disks_in_config);
    }
  }

  /* Practically just took all bad_disks offline so update the stats. */
  RecSetGlobalRawStatSum(cache_rsb, cache_span_offline_stat, bad_disks);
  RecIncrGlobalRawStat(cache_rsb, cache_span_failing_stat, -bad_disks);
  RecSetGlobalRawStatSum(cache_rsb, cache_span_online_stat, gndisks);

  /* create the cachevol list only if num volumes are greater than 0. */
  if (config_volumes.num_volumes == 0) {
    /* if no volumes, default to just an http cache */
    res = cplist_reconfigure();
  } else {
    // else
    /* create the cachevol list. */
    cplist_init();
    /* now change the cachevol list based on the config file */
    res = cplist_reconfigure();
  }

  if (res == -1) {
    /* problems initializing the volume.config. Punt */
    gnvol = 0;
    cacheInitialized();
    return;
  } else {
    CacheVol *cp = cp_list.head;
    for (; cp; cp = cp->link.next) {
      cp->vol_rsb = RecAllocateRawStatBlock((int)cache_stat_count);
      char vol_stat_str_prefix[256];
      snprintf(vol_stat_str_prefix, sizeof(vol_stat_str_prefix), "proxy.process.cache.volume_%d", cp->vol_number);
      register_cache_stats(cp->vol_rsb, vol_stat_str_prefix);
    }
  }

  gvol = (Vol **)ats_malloc(gnvol * sizeof(Vol *));
  memset(gvol, 0, gnvol * sizeof(Vol *));
  gnvol = 0;
  for (i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    if (is_debug_tag_set("cache_hosting")) {
      int j;
      Debug("cache_hosting", "Disk: %d:%s: Vol Blocks: %u: Free space: %" PRIu64, i, d->path, d->header->num_diskvol_blks,
            d->free_space);
      for (j = 0; j < (int)d->header->num_volumes; j++) {
        Debug("cache_hosting", "\tVol: %d Size: %" PRIu64, d->disk_vols[j]->vol_number, d->disk_vols[j]->size);
      }
      for (j = 0; j < (int)d->header->num_diskvol_blks; j++) {
        Debug("cache_hosting", "\tBlock No: %d Size: %" PRIu64 " Free: %u", d->header->vol_info[j].number,
              d->header->vol_info[j].len, d->header->vol_info[j].free);
      }
    }
    if (!check) {
      d->sync();
    }
  }
  if (config_volumes.num_volumes == 0) {
    theCache         = new Cache();
    theCache->scheme = CACHE_HTTP_TYPE;
    theCache->open(clear, fix);
    return;
  }
  if (config_volumes.num_http_volumes != 0) {
    theCache         = new Cache();
    theCache->scheme = CACHE_HTTP_TYPE;
    theCache->open(clear, fix);
  }
}

void
CacheProcessor::cacheInitialized()
{
  int i;

  if (theCache && (theCache->ready == CACHE_INITIALIZING)) {
    return;
  }

  int caches_ready  = 0;
  int cache_init_ok = 0;
  /* allocate ram size in proportion to the disk space the
     volume accupies */
  int64_t total_size             = 0; // count in HTTP & MIXT
  uint64_t total_cache_bytes     = 0; // bytes that can used in total_size
  uint64_t total_direntries      = 0; // all the direntries in the cache
  uint64_t used_direntries       = 0; //   and used
  uint64_t vol_total_cache_bytes = 0;
  uint64_t vol_total_direntries  = 0;
  uint64_t vol_used_direntries   = 0;
  Vol *vol;

  ProxyMutex *mutex = this_ethread()->mutex.get();

  if (theCache) {
    total_size += theCache->cache_size;
    Debug("cache_init", "CacheProcessor::cacheInitialized - theCache, total_size = %" PRId64 " = %" PRId64 " MB", total_size,
          total_size / ((1024 * 1024) / STORE_BLOCK_SIZE));
    if (theCache->ready == CACHE_INIT_FAILED) {
      Debug("cache_init", "CacheProcessor::cacheInitialized - failed to initialize the cache for http: cache disabled");
      Warning("failed to initialize the cache for http: cache disabled\n");
    } else {
      caches_ready                 = caches_ready | (1 << CACHE_FRAG_TYPE_HTTP);
      caches_ready                 = caches_ready | (1 << CACHE_FRAG_TYPE_NONE);
      caches[CACHE_FRAG_TYPE_HTTP] = theCache;
      caches[CACHE_FRAG_TYPE_NONE] = theCache;
    }
  }

  // Update stripe version data.
  if (gnvol) { // start with whatever the first stripe is.
    cacheProcessor.min_stripe_version = cacheProcessor.max_stripe_version = gvol[0]->header->version;
  }
  // scan the rest of the stripes.
  for (i = 1; i < gnvol; i++) {
    Vol *v = gvol[i];
    if (v->header->version < cacheProcessor.min_stripe_version) {
      cacheProcessor.min_stripe_version = v->header->version;
    }
    if (cacheProcessor.max_stripe_version < v->header->version) {
      cacheProcessor.max_stripe_version = v->header->version;
    }
  }

  if (caches_ready) {
    Debug("cache_init", "CacheProcessor::cacheInitialized - caches_ready=0x%0X, gnvol=%d", (unsigned int)caches_ready,
          gnvol.load());

    int64_t ram_cache_bytes = 0;

    if (gnvol) {
      // new ram_caches, with algorithm from the config
      for (i = 0; i < gnvol; i++) {
        switch (cache_config_ram_cache_algorithm) {
        default:
        case RAM_CACHE_ALGORITHM_CLFUS:
          gvol[i]->ram_cache = new_RamCacheCLFUS();
          break;
        case RAM_CACHE_ALGORITHM_LRU:
          gvol[i]->ram_cache = new_RamCacheLRU();
          break;
        }
      }
      // let us calculate the Size
      if (cache_config_ram_cache_size == AUTO_SIZE_RAM_CACHE) {
        Debug("cache_init", "CacheProcessor::cacheInitialized - cache_config_ram_cache_size == AUTO_SIZE_RAM_CACHE");
        for (i = 0; i < gnvol; i++) {
          vol = gvol[i];
          gvol[i]->ram_cache->init(vol->dirlen() * DEFAULT_RAM_CACHE_MULTIPLIER, vol);
          ram_cache_bytes += gvol[i]->dirlen();
          Debug("cache_init", "CacheProcessor::cacheInitialized - ram_cache_bytes = %" PRId64 " = %" PRId64 "Mb", ram_cache_bytes,
                ram_cache_bytes / (1024 * 1024));
          CACHE_VOL_SUM_DYN_STAT(cache_ram_cache_bytes_total_stat, (int64_t)gvol[i]->dirlen());

          vol_total_cache_bytes = gvol[i]->len - gvol[i]->dirlen();
          total_cache_bytes += vol_total_cache_bytes;
          Debug("cache_init", "CacheProcessor::cacheInitialized - total_cache_bytes = %" PRId64 " = %" PRId64 "Mb",
                total_cache_bytes, total_cache_bytes / (1024 * 1024));

          CACHE_VOL_SUM_DYN_STAT(cache_bytes_total_stat, vol_total_cache_bytes);

          vol_total_direntries = gvol[i]->buckets * gvol[i]->segments * DIR_DEPTH;
          total_direntries += vol_total_direntries;
          CACHE_VOL_SUM_DYN_STAT(cache_direntries_total_stat, vol_total_direntries);

          vol_used_direntries = dir_entries_used(gvol[i]);
          CACHE_VOL_SUM_DYN_STAT(cache_direntries_used_stat, vol_used_direntries);
          used_direntries += vol_used_direntries;
        }

      } else {
        // we got configured memory size
        // TODO, should we check the available system memories, or you will
        //   OOM or swapout, that is not a good situation for the server
        Debug("cache_init", "CacheProcessor::cacheInitialized - %" PRId64 " != AUTO_SIZE_RAM_CACHE", cache_config_ram_cache_size);
        int64_t http_ram_cache_size =
          (theCache) ? (int64_t)(((double)theCache->cache_size / total_size) * cache_config_ram_cache_size) : 0;
        Debug("cache_init", "CacheProcessor::cacheInitialized - http_ram_cache_size = %" PRId64 " = %" PRId64 "Mb",
              http_ram_cache_size, http_ram_cache_size / (1024 * 1024));
        int64_t stream_ram_cache_size = cache_config_ram_cache_size - http_ram_cache_size;
        Debug("cache_init", "CacheProcessor::cacheInitialized - stream_ram_cache_size = %" PRId64 " = %" PRId64 "Mb",
              stream_ram_cache_size, stream_ram_cache_size / (1024 * 1024));

        // Dump some ram_cache size information in debug mode.
        Debug("ram_cache", "config: size = %" PRId64 ", cutoff = %" PRId64 "", cache_config_ram_cache_size,
              cache_config_ram_cache_cutoff);

        for (i = 0; i < gnvol; i++) {
          vol = gvol[i];
          double factor;
          if (gvol[i]->cache == theCache) {
            ink_assert(gvol[i]->cache != nullptr);
            factor = (double)(int64_t)(gvol[i]->len >> STORE_BLOCK_SHIFT) / (int64_t)theCache->cache_size;
            Debug("cache_init", "CacheProcessor::cacheInitialized - factor = %f", factor);
            gvol[i]->ram_cache->init((int64_t)(http_ram_cache_size * factor), vol);
            ram_cache_bytes += (int64_t)(http_ram_cache_size * factor);
            CACHE_VOL_SUM_DYN_STAT(cache_ram_cache_bytes_total_stat, (int64_t)(http_ram_cache_size * factor));
          } else {
            ink_release_assert(!"Unexpected non-HTTP cache volume");
          }
          Debug("cache_init", "CacheProcessor::cacheInitialized[%d] - ram_cache_bytes = %" PRId64 " = %" PRId64 "Mb", i,
                ram_cache_bytes, ram_cache_bytes / (1024 * 1024));
          vol_total_cache_bytes = gvol[i]->len - gvol[i]->dirlen();
          total_cache_bytes += vol_total_cache_bytes;
          CACHE_VOL_SUM_DYN_STAT(cache_bytes_total_stat, vol_total_cache_bytes);
          Debug("cache_init", "CacheProcessor::cacheInitialized - total_cache_bytes = %" PRId64 " = %" PRId64 "Mb",
                total_cache_bytes, total_cache_bytes / (1024 * 1024));

          vol_total_direntries = gvol[i]->buckets * gvol[i]->segments * DIR_DEPTH;
          total_direntries += vol_total_direntries;
          CACHE_VOL_SUM_DYN_STAT(cache_direntries_total_stat, vol_total_direntries);

          vol_used_direntries = dir_entries_used(gvol[i]);
          CACHE_VOL_SUM_DYN_STAT(cache_direntries_used_stat, vol_used_direntries);
          used_direntries += vol_used_direntries;
        }
      }
      switch (cache_config_ram_cache_compress) {
      default:
        Fatal("unknown RAM cache compression type: %d", cache_config_ram_cache_compress);
      case CACHE_COMPRESSION_NONE:
      case CACHE_COMPRESSION_FASTLZ:
        break;
      case CACHE_COMPRESSION_LIBZ:
#ifndef HAVE_ZLIB_H
        Fatal("libz not available for RAM cache compression");
#endif
        break;
      case CACHE_COMPRESSION_LIBLZMA:
#ifndef HAVE_LZMA_H
        Fatal("lzma not available for RAM cache compression");
#endif
        break;
      }

      GLOBAL_CACHE_SET_DYN_STAT(cache_ram_cache_bytes_total_stat, ram_cache_bytes);
      GLOBAL_CACHE_SET_DYN_STAT(cache_bytes_total_stat, total_cache_bytes);
      GLOBAL_CACHE_SET_DYN_STAT(cache_direntries_total_stat, total_direntries);
      GLOBAL_CACHE_SET_DYN_STAT(cache_direntries_used_stat, used_direntries);
      if (!check) {
        dir_sync_init();
      }
      cache_init_ok = 1;
    } else {
      Warning("cache unable to open any vols, disabled");
    }
  }
  if (cache_init_ok) {
    // Initialize virtual cache
    CacheProcessor::initialized = CACHE_INITIALIZED;
    CacheProcessor::cache_ready = caches_ready;
    Note("cache enabled");
  } else {
    CacheProcessor::initialized = CACHE_INIT_FAILED;
    Note("cache disabled");
  }

  // Fire callback to signal initialization finished.
  if (cb_after_init) {
    cb_after_init();
  }

  // TS-3848
  if (CACHE_INIT_FAILED == CacheProcessor::initialized && cacheProcessor.waitForCache() > 1) {
    Fatal("Cache initialization failed with cache required, exiting.");
  }
}

void
CacheProcessor::stop()
{
}

int
CacheProcessor::dir_check(bool afix)
{
  for (int i = 0; i < gnvol; i++) {
    gvol[i]->dir_check(afix);
  }
  return 0;
}

int
CacheProcessor::db_check(bool afix)
{
  for (int i = 0; i < gnvol; i++) {
    gvol[i]->db_check(afix);
  }
  return 0;
}

Action *
CacheProcessor::lookup(Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname, int host_len)
{
  return caches[frag_type]->lookup(cont, key, frag_type, hostname, host_len);
}

inkcoreapi Action *
CacheProcessor::open_read(Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname, int hostlen)
{
  return caches[frag_type]->open_read(cont, key, frag_type, hostname, hostlen);
}

inkcoreapi Action *
CacheProcessor::open_write(Continuation *cont, CacheKey *key, CacheFragType frag_type, int expected_size ATS_UNUSED, int options,
                           time_t pin_in_cache, char *hostname, int host_len)
{
  return caches[frag_type]->open_write(cont, key, frag_type, options, pin_in_cache, hostname, host_len);
}

Action *
CacheProcessor::remove(Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname, int host_len)
{
  Debug("cache_remove", "[CacheProcessor::remove] Issuing cache delete for %u", cache_hash(*key));
  return caches[frag_type]->remove(cont, key, frag_type, hostname, host_len);
}

Action *
CacheProcessor::lookup(Continuation *cont, const HttpCacheKey *key, CacheFragType frag_type)
{
  return lookup(cont, &key->hash, frag_type, key->hostname, key->hostlen);
}

Action *
CacheProcessor::scan(Continuation *cont, char *hostname, int host_len, int KB_per_second)
{
  return caches[CACHE_FRAG_TYPE_HTTP]->scan(cont, hostname, host_len, KB_per_second);
}

int
CacheProcessor::IsCacheEnabled()
{
  return CacheProcessor::initialized;
}

bool
CacheProcessor::IsCacheReady(CacheFragType type)
{
  if (IsCacheEnabled() != CACHE_INITIALIZED) {
    return false;
  }
  return (bool)(cache_ready & (1 << type));
}

int
Vol::db_check(bool /* fix ATS_UNUSED */)
{
  char tt[256];
  printf("    Data for [%s]\n", hash_text.get());
  printf("        Length:          %" PRIu64 "\n", (uint64_t)len);
  printf("        Write Position:  %" PRIu64 "\n", (uint64_t)(header->write_pos - skip));
  printf("        Phase:           %d\n", (int)!!header->phase);
  ink_ctime_r(&header->create_time, tt);
  tt[strlen(tt) - 1] = 0;
  printf("        Create Time:     %s\n", tt);
  printf("        Sync Serial:     %u\n", (unsigned int)header->sync_serial);
  printf("        Write Serial:    %u\n", (unsigned int)header->write_serial);
  printf("\n");

  return 0;
}

static void
vol_init_data_internal(Vol *d)
{
  // step1: calculate the number of entries.
  off_t total_entries = (d->len - (d->start - d->skip)) / cache_config_min_average_object_size;
  // step2: calculate the number of buckets
  off_t total_buckets = total_entries / DIR_DEPTH;
  // step3: calculate the number of segments, no semgent has more than 16384 buckets
  d->segments = (total_buckets + (((1 << 16) - 1) / DIR_DEPTH)) / ((1 << 16) / DIR_DEPTH);
  // step4: divide total_buckets into segments on average.
  d->buckets = (total_buckets + d->segments - 1) / d->segments;
  // step5: set the start pointer.
  d->start = d->skip + 2 * d->dirlen();
}

static void
vol_init_data(Vol *d)
{
  // iteratively calculate start + buckets
  vol_init_data_internal(d);
  vol_init_data_internal(d);
  vol_init_data_internal(d);
}

void
vol_init_dir(Vol *d)
{
  int b, s, l;

  for (s = 0; s < d->segments; s++) {
    d->header->freelist[s] = 0;
    Dir *seg               = d->dir_segment(s);
    for (l = 1; l < DIR_DEPTH; l++) {
      for (b = 0; b < d->buckets; b++) {
        Dir *bucket = dir_bucket(b, seg);
        dir_free_entry(dir_bucket_row(bucket, l), s, d);
      }
    }
  }
}

void
vol_clear_init(Vol *d)
{
  size_t dir_len = d->dirlen();
  memset(d->raw_dir, 0, dir_len);
  vol_init_dir(d);
  d->header->magic          = VOL_MAGIC;
  d->header->version._major = CACHE_DB_MAJOR_VERSION;
  d->header->version._minor = CACHE_DB_MINOR_VERSION;
  d->scan_pos = d->header->agg_pos = d->header->write_pos = d->start;
  d->header->last_write_pos                               = d->header->write_pos;
  d->header->phase                                        = 0;
  d->header->cycle                                        = 0;
  d->header->create_time                                  = time(nullptr);
  d->header->dirty                                        = 0;
  d->sector_size = d->header->sector_size = d->disk->hw_sector_size;
  *d->footer                              = *d->header;
}

int
vol_dir_clear(Vol *d)
{
  size_t dir_len = d->dirlen();
  vol_clear_init(d);

  if (pwrite(d->fd, d->raw_dir, dir_len, d->skip) < 0) {
    Warning("unable to clear cache directory '%s'", d->hash_text.get());
    return -1;
  }
  return 0;
}

int
Vol::clear_dir()
{
  size_t dir_len = this->dirlen();
  vol_clear_init(this);

  SET_HANDLER(&Vol::handle_dir_clear);

  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_buf    = raw_dir;
  io.aiocb.aio_nbytes = dir_len;
  io.aiocb.aio_offset = skip;
  io.action           = this;
  io.thread           = AIO_CALLBACK_THREAD_ANY;
  io.then             = nullptr;
  ink_assert(ink_aio_write(&io));
  return 0;
}

int
Vol::init(char *s, off_t blocks, off_t dir_skip, bool clear)
{
  char *seed_str              = disk->hash_base_string ? disk->hash_base_string : s;
  const size_t hash_seed_size = strlen(seed_str);
  const size_t hash_text_size = hash_seed_size + 32;

  hash_text = static_cast<char *>(ats_malloc(hash_text_size));
  ink_strlcpy(hash_text, seed_str, hash_text_size);
  snprintf(hash_text + hash_seed_size, (hash_text_size - hash_seed_size), " %" PRIu64 ":%" PRIu64 "", (uint64_t)dir_skip,
           (uint64_t)blocks);
  CryptoContext().hash_immediate(hash_id, hash_text, strlen(hash_text));

  dir_skip = ROUND_TO_STORE_BLOCK((dir_skip < START_POS ? START_POS : dir_skip));
  path     = ats_strdup(s);
  len      = blocks * STORE_BLOCK_SIZE;
  ink_assert(len <= MAX_VOL_SIZE);
  skip             = dir_skip;
  prev_recover_pos = 0;

  // successive approximation, directory/meta data eats up some storage
  start = dir_skip;
  vol_init_data(this);
  data_blocks         = (len - (start - skip)) / STORE_BLOCK_SIZE;
  hit_evacuate_window = (data_blocks * cache_config_hit_evacuate_percent) / 100;

  evacuate_size = (int)(len / EVACUATION_BUCKET_SIZE) + 2;
  int evac_len  = (int)evacuate_size * sizeof(DLL<EvacuationBlock>);
  evacuate      = (DLL<EvacuationBlock> *)ats_malloc(evac_len);
  memset(static_cast<void *>(evacuate), 0, evac_len);

  Debug("cache_init", "Vol %s: allocating %zu directory bytes for a %lld byte volume (%lf%%)", hash_text.get(), dirlen(),
        (long long)this->len, (double)dirlen() / (double)this->len * 100.0);

  raw_dir = nullptr;
  if (ats_hugepage_enabled()) {
    raw_dir = (char *)ats_alloc_hugepage(this->dirlen());
  }
  if (raw_dir == nullptr) {
    raw_dir = (char *)ats_memalign(ats_pagesize(), this->dirlen());
  }

  dir    = (Dir *)(raw_dir + this->headerlen());
  header = (VolHeaderFooter *)raw_dir;
  footer = (VolHeaderFooter *)(raw_dir + this->dirlen() - ROUND_TO_STORE_BLOCK(sizeof(VolHeaderFooter)));

  if (clear) {
    Note("clearing cache directory '%s'", hash_text.get());
    return clear_dir();
  }

  init_info           = new VolInitInfo();
  int footerlen       = ROUND_TO_STORE_BLOCK(sizeof(VolHeaderFooter));
  off_t footer_offset = this->dirlen() - footerlen;
  // try A
  off_t as = skip;

  Debug("cache_init", "reading directory '%s'", hash_text.get());
  SET_HANDLER(&Vol::handle_header_read);
  init_info->vol_aio[0].aiocb.aio_offset = as;
  init_info->vol_aio[1].aiocb.aio_offset = as + footer_offset;
  off_t bs                               = skip + this->dirlen();
  init_info->vol_aio[2].aiocb.aio_offset = bs;
  init_info->vol_aio[3].aiocb.aio_offset = bs + footer_offset;

  for (unsigned i = 0; i < countof(init_info->vol_aio); i++) {
    AIOCallback *aio      = &(init_info->vol_aio[i]);
    aio->aiocb.aio_fildes = fd;
    aio->aiocb.aio_buf    = &(init_info->vol_h_f[i * STORE_BLOCK_SIZE]);
    aio->aiocb.aio_nbytes = footerlen;
    aio->action           = this;
    aio->thread           = AIO_CALLBACK_THREAD_ANY;
    aio->then             = (i < 3) ? &(init_info->vol_aio[i + 1]) : nullptr;
  }
#if AIO_MODE == AIO_MODE_NATIVE
  ink_assert(ink_aio_readv(init_info->vol_aio));
#else
  ink_assert(ink_aio_read(init_info->vol_aio));
#endif
  return 0;
}

int
Vol::handle_dir_clear(int event, void *data)
{
  size_t dir_len = this->dirlen();
  AIOCallback *op;

  if (event == AIO_EVENT_DONE) {
    op = (AIOCallback *)data;
    if ((size_t)op->aio_result != (size_t)op->aiocb.aio_nbytes) {
      Warning("unable to clear cache directory '%s'", hash_text.get());
      disk->incrErrors(op);
      fd = -1;
    }

    if (op->aiocb.aio_nbytes == dir_len) {
      /* clear the header for directory B. We don't need to clear the
         whole of directory B. The header for directory B starts at
         skip + len */
      op->aiocb.aio_nbytes = ROUND_TO_STORE_BLOCK(sizeof(VolHeaderFooter));
      op->aiocb.aio_offset = skip + dir_len;
      ink_assert(ink_aio_write(op));
      return EVENT_DONE;
    }
    set_io_not_in_progress();
    SET_HANDLER(&Vol::dir_init_done);
    dir_init_done(EVENT_IMMEDIATE, nullptr);
    /* mark the volume as bad */
  }
  return EVENT_DONE;
}

int
Vol::handle_dir_read(int event, void *data)
{
  AIOCallback *op = (AIOCallback *)data;

  if (event == AIO_EVENT_DONE) {
    if ((size_t)op->aio_result != (size_t)op->aiocb.aio_nbytes) {
      Note("Directory read failed: clearing cache directory %s", this->hash_text.get());
      clear_dir();
      return EVENT_DONE;
    }
  }

  if (!(header->magic == VOL_MAGIC && footer->magic == VOL_MAGIC && CACHE_DB_MAJOR_VERSION_COMPATIBLE <= header->version._major &&
        header->version._major <= CACHE_DB_MAJOR_VERSION)) {
    Warning("bad footer in cache directory for '%s', clearing", hash_text.get());
    Note("VOL_MAGIC %d\n header magic: %d\n footer_magic %d\n CACHE_DB_MAJOR_VERSION_COMPATIBLE %d\n major version %d\n"
         "CACHE_DB_MAJOR_VERSION %d\n",
         VOL_MAGIC, header->magic, footer->magic, CACHE_DB_MAJOR_VERSION_COMPATIBLE, header->version._major,
         CACHE_DB_MAJOR_VERSION);
    Note("clearing cache directory '%s'", hash_text.get());
    clear_dir();
    return EVENT_DONE;
  }
  CHECK_DIR(this);

  sector_size = header->sector_size;

  return this->recover_data();

  return EVENT_CONT;
}

int
Vol::recover_data()
{
  SET_HANDLER(&Vol::handle_recover_from_data);
  return handle_recover_from_data(EVENT_IMMEDIATE, nullptr);
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
Vol::handle_recover_from_data(int event, void * /* data ATS_UNUSED */)
{
  uint32_t got_len         = 0;
  uint32_t max_sync_serial = header->sync_serial;
  char *s, *e;
  if (event == EVENT_IMMEDIATE) {
    if (header->sync_serial == 0) {
      io.aiocb.aio_buf = nullptr;
      SET_HANDLER(&Vol::handle_recover_write_dir);
      return handle_recover_write_dir(EVENT_IMMEDIATE, nullptr);
    }
    // initialize
    recover_wrapped   = false;
    last_sync_serial  = 0;
    last_write_serial = 0;
    recover_pos       = header->last_write_pos;
    if (recover_pos >= skip + len) {
      recover_wrapped = true;
      recover_pos     = start;
    }
    io.aiocb.aio_buf    = (char *)ats_memalign(ats_pagesize(), RECOVERY_SIZE);
    io.aiocb.aio_nbytes = RECOVERY_SIZE;
    if ((off_t)(recover_pos + io.aiocb.aio_nbytes) > (off_t)(skip + len)) {
      io.aiocb.aio_nbytes = (skip + len) - recover_pos;
    }
  } else if (event == AIO_EVENT_DONE) {
    if ((size_t)io.aiocb.aio_nbytes != (size_t)io.aio_result) {
      Warning("disk read error on recover '%s', clearing", hash_text.get());
      disk->incrErrors(&io);
      goto Lclear;
    }
    if (io.aiocb.aio_offset == header->last_write_pos) {
      /* check that we haven't wrapped around without syncing
         the directory. Start from last_write_serial (write pos the documents
         were written to just before syncing the directory) and make sure
         that all documents have write_serial <= header->write_serial.
       */
      uint32_t to_check = header->write_pos - header->last_write_pos;
      ink_assert(to_check && to_check < (uint32_t)io.aiocb.aio_nbytes);
      uint32_t done = 0;
      s             = (char *)io.aiocb.aio_buf;
      while (done < to_check) {
        Doc *doc = (Doc *)(s + done);
        if (doc->magic != DOC_MAGIC || doc->write_serial > header->write_serial) {
          Warning("no valid directory found while recovering '%s', clearing", hash_text.get());
          goto Lclear;
        }
        done += round_to_approx_size(doc->len);
        if (doc->sync_serial > last_write_serial) {
          last_sync_serial = doc->sync_serial;
        }
      }
      ink_assert(done == to_check);

      got_len = io.aiocb.aio_nbytes - done;
      recover_pos += io.aiocb.aio_nbytes;
      s = (char *)io.aiocb.aio_buf + done;
      e = s + got_len;
    } else {
      got_len = io.aiocb.aio_nbytes;
      recover_pos += io.aiocb.aio_nbytes;
      s = (char *)io.aiocb.aio_buf;
      e = s + got_len;
    }
  }
  // examine what we got
  if (got_len) {
    Doc *doc = nullptr;

    if (recover_wrapped && start == io.aiocb.aio_offset) {
      doc = (Doc *)s;
      if (doc->magic != DOC_MAGIC || doc->write_serial < last_write_serial) {
        recover_pos = skip + len - EVACUATION_SIZE;
        goto Ldone;
      }
    }

    while (s < e) {
      doc = (Doc *)s;

      if (doc->magic != DOC_MAGIC || doc->sync_serial != last_sync_serial) {
        if (doc->magic == DOC_MAGIC) {
          if (doc->sync_serial > header->sync_serial) {
            max_sync_serial = doc->sync_serial;
          }

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
            recover_wrapped     = true;
            recover_pos         = start;
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
            recover_wrapped     = true;
            recover_pos         = start;
            io.aiocb.aio_nbytes = RECOVERY_SIZE;

            break;
          }
          // we ar not in the danger zone
          goto Ldone;
        }
      }
      // doc->magic == DOC_MAGIC && doc->sync_serial == last_sync_serial
      last_write_serial = doc->write_serial;
      s += round_to_approx_size(doc->len);
    }

    /* if (s > e) then we gone through RECOVERY_SIZE; we need to
       read more data off disk and continue recovering */
    if (s >= e) {
      /* In the last iteration, we increment s by doc->len...need to undo
         that change */
      if (s > e) {
        s -= round_to_approx_size(doc->len);
      }
      recover_pos -= e - s;
      if (recover_pos >= skip + len) {
        recover_wrapped = true;
        recover_pos     = start;
      }
      io.aiocb.aio_nbytes = RECOVERY_SIZE;
      if ((off_t)(recover_pos + io.aiocb.aio_nbytes) > (off_t)(skip + len)) {
        io.aiocb.aio_nbytes = (skip + len) - recover_pos;
      }
    }
  }
  if (recover_pos == prev_recover_pos) { // this should never happen, but if it does break the loop
    goto Lclear;
  }
  prev_recover_pos    = recover_pos;
  io.aiocb.aio_offset = recover_pos;
  ink_assert(ink_aio_read(&io));
  return EVENT_CONT;

Ldone : {
  /* if we come back to the starting position, then we don't have to recover anything */
  if (recover_pos == header->write_pos && recover_wrapped) {
    SET_HANDLER(&Vol::handle_recover_write_dir);
    if (is_debug_tag_set("cache_init")) {
      Note("recovery wrapped around. nothing to clear\n");
    }
    return handle_recover_write_dir(EVENT_IMMEDIATE, nullptr);
  }

  recover_pos += EVACUATION_SIZE; // safely cover the max write size
  if (recover_pos < header->write_pos && (recover_pos + EVACUATION_SIZE >= header->write_pos)) {
    Debug("cache_init", "Head Pos: %" PRIu64 ", Rec Pos: %" PRIu64 ", Wrapped:%d", header->write_pos, recover_pos, recover_wrapped);
    Warning("no valid directory found while recovering '%s', clearing", hash_text.get());
    goto Lclear;
  }

  if (recover_pos > skip + len) {
    recover_pos -= skip + len;
  }
  // bump sync number so it is different from that in the Doc structs
  uint32_t next_sync_serial = max_sync_serial + 1;
  // make that the next sync does not overwrite our good copy!
  if (!(header->sync_serial & 1) == !(next_sync_serial & 1)) {
    next_sync_serial++;
  }
  // clear effected portion of the cache
  off_t clear_start = this->offset_to_vol_offset(header->write_pos);
  off_t clear_end   = this->offset_to_vol_offset(recover_pos);
  if (clear_start <= clear_end) {
    dir_clear_range(clear_start, clear_end, this);
  } else {
    dir_clear_range(clear_end, DIR_OFFSET_MAX, this);
    dir_clear_range(1, clear_start, this);
  }

  Note("recovery clearing offsets of Vol %s : [%" PRIu64 ", %" PRIu64 "] sync_serial %d next %d\n", hash_text.get(),
       header->write_pos, recover_pos, header->sync_serial, next_sync_serial);

  footer->sync_serial = header->sync_serial = next_sync_serial;

  for (int i = 0; i < 3; i++) {
    AIOCallback *aio      = &(init_info->vol_aio[i]);
    aio->aiocb.aio_fildes = fd;
    aio->action           = this;
    aio->thread           = AIO_CALLBACK_THREAD_ANY;
    aio->then             = (i < 2) ? &(init_info->vol_aio[i + 1]) : nullptr;
  }
  int footerlen = ROUND_TO_STORE_BLOCK(sizeof(VolHeaderFooter));
  size_t dirlen = this->dirlen();
  int B         = header->sync_serial & 1;
  off_t ss      = skip + (B ? dirlen : 0);

  init_info->vol_aio[0].aiocb.aio_buf    = raw_dir;
  init_info->vol_aio[0].aiocb.aio_nbytes = footerlen;
  init_info->vol_aio[0].aiocb.aio_offset = ss;
  init_info->vol_aio[1].aiocb.aio_buf    = raw_dir + footerlen;
  init_info->vol_aio[1].aiocb.aio_nbytes = dirlen - 2 * footerlen;
  init_info->vol_aio[1].aiocb.aio_offset = ss + footerlen;
  init_info->vol_aio[2].aiocb.aio_buf    = raw_dir + dirlen - footerlen;
  init_info->vol_aio[2].aiocb.aio_nbytes = footerlen;
  init_info->vol_aio[2].aiocb.aio_offset = ss + dirlen - footerlen;

  SET_HANDLER(&Vol::handle_recover_write_dir);
#if AIO_MODE == AIO_MODE_NATIVE
  ink_assert(ink_aio_writev(init_info->vol_aio));
#else
  ink_assert(ink_aio_write(init_info->vol_aio));
#endif
  return EVENT_CONT;
}

Lclear:
  free((char *)io.aiocb.aio_buf);
  delete init_info;
  init_info = nullptr;
  clear_dir();
  return EVENT_CONT;
}

int
Vol::handle_recover_write_dir(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (io.aiocb.aio_buf) {
    free((char *)io.aiocb.aio_buf);
  }
  delete init_info;
  init_info = nullptr;
  set_io_not_in_progress();
  scan_pos = header->write_pos;
  periodic_scan();
  SET_HANDLER(&Vol::dir_init_done);
  return dir_init_done(EVENT_IMMEDIATE, nullptr);
}

int
Vol::handle_header_read(int event, void *data)
{
  AIOCallback *op;
  VolHeaderFooter *hf[4];
  switch (event) {
  case AIO_EVENT_DONE:
    op = (AIOCallback *)data;
    for (auto &i : hf) {
      ink_assert(op != nullptr);
      i = (VolHeaderFooter *)(op->aiocb.aio_buf);
      if ((size_t)op->aio_result != (size_t)op->aiocb.aio_nbytes) {
        Note("Header read failed: clearing cache directory %s", this->hash_text.get());
        clear_dir();
        return EVENT_DONE;
      }
      op = op->then;
    }

    io.aiocb.aio_fildes = fd;
    io.aiocb.aio_nbytes = this->dirlen();
    io.aiocb.aio_buf    = raw_dir;
    io.action           = this;
    io.thread           = AIO_CALLBACK_THREAD_ANY;
    io.then             = nullptr;

    if (hf[0]->sync_serial == hf[1]->sync_serial &&
        (hf[0]->sync_serial >= hf[2]->sync_serial || hf[2]->sync_serial != hf[3]->sync_serial)) {
      SET_HANDLER(&Vol::handle_dir_read);
      if (is_debug_tag_set("cache_init")) {
        Note("using directory A for '%s'", hash_text.get());
      }
      io.aiocb.aio_offset = skip;
      ink_assert(ink_aio_read(&io));
    }
    // try B
    else if (hf[2]->sync_serial == hf[3]->sync_serial) {
      SET_HANDLER(&Vol::handle_dir_read);
      if (is_debug_tag_set("cache_init")) {
        Note("using directory B for '%s'", hash_text.get());
      }
      io.aiocb.aio_offset = skip + this->dirlen();
      ink_assert(ink_aio_read(&io));
    } else {
      Note("no good directory, clearing '%s' since sync_serials on both A and B copies are invalid", hash_text.get());
      Note("Header A: %d\nFooter A: %d\n Header B: %d\n Footer B %d\n", hf[0]->sync_serial, hf[1]->sync_serial, hf[2]->sync_serial,
           hf[3]->sync_serial);
      clear_dir();
      delete init_info;
      init_info = nullptr;
    }
    return EVENT_DONE;
  default:
    ink_assert(!"not reach here");
  }
  return EVENT_DONE;
}

int
Vol::dir_init_done(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (!cache->cache_read_done) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(5), ET_CALL);
    return EVENT_CONT;
  } else {
    int vol_no = gnvol++;
    ink_assert(!gvol[vol_no]);
    gvol[vol_no] = this;
    SET_HANDLER(&Vol::aggWrite);
    if (fd == -1) {
      cache->vol_initialized(false);
    } else {
      cache->vol_initialized(true);
    }
    return EVENT_DONE;
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

void
build_vol_hash_table(CacheHostRecord *cp)
{
  int num_vols          = cp->num_vols;
  unsigned int *mapping = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_vols);
  Vol **p               = (Vol **)ats_malloc(sizeof(Vol *) * num_vols);

  memset(mapping, 0, num_vols * sizeof(unsigned int));
  memset(p, 0, num_vols * sizeof(Vol *));
  uint64_t total = 0;
  int bad_vols   = 0;
  int map        = 0;
  uint64_t used  = 0;
  // initialize number of elements per vol
  for (int i = 0; i < num_vols; i++) {
    if (DISK_BAD(cp->vols[i]->disk)) {
      bad_vols++;
      continue;
    }
    mapping[map] = i;
    p[map++]     = cp->vols[i];
    total += (cp->vols[i]->len >> STORE_BLOCK_SHIFT);
  }

  num_vols -= bad_vols;

  if (!num_vols || !total) {
    // all the disks are corrupt,
    if (cp->vol_hash_table) {
      new_Freer(cp->vol_hash_table, CACHE_MEM_FREE_TIMEOUT);
    }
    cp->vol_hash_table = nullptr;
    ats_free(mapping);
    ats_free(p);
    return;
  }

  unsigned int *forvol   = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_vols);
  unsigned int *gotvol   = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_vols);
  unsigned int *rnd      = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_vols);
  unsigned short *ttable = (unsigned short *)ats_malloc(sizeof(unsigned short) * VOL_HASH_TABLE_SIZE);
  unsigned short *old_table;
  unsigned int *rtable_entries = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_vols);
  unsigned int rtable_size     = 0;

  // estimate allocation
  for (int i = 0; i < num_vols; i++) {
    forvol[i] = (VOL_HASH_TABLE_SIZE * (p[i]->len >> STORE_BLOCK_SHIFT)) / total;
    used += forvol[i];
    rtable_entries[i] = p[i]->len / VOL_HASH_ALLOC_SIZE;
    rtable_size += rtable_entries[i];
    gotvol[i] = 0;
  }
  // spread around the excess
  int extra = VOL_HASH_TABLE_SIZE - used;
  for (int i = 0; i < extra; i++) {
    forvol[i % num_vols]++;
  }
  // seed random number generator
  for (int i = 0; i < num_vols; i++) {
    uint64_t x = p[i]->hash_id.fold();
    rnd[i]     = (unsigned int)x;
  }
  // initialize table to "empty"
  for (int i = 0; i < VOL_HASH_TABLE_SIZE; i++) {
    ttable[i] = VOL_HASH_EMPTY;
  }
  // generate random numbers proportaion to allocation
  rtable_pair *rtable = (rtable_pair *)ats_malloc(sizeof(rtable_pair) * rtable_size);
  int rindex          = 0;
  for (int i = 0; i < num_vols; i++) {
    for (int j = 0; j < (int)rtable_entries[i]; j++) {
      rtable[rindex].rval = next_rand(&rnd[i]);
      rtable[rindex].idx  = i;
      rindex++;
    }
  }
  ink_assert(rindex == (int)rtable_size);
  // sort (rand #, vol $ pairs)
  qsort(rtable, rtable_size, sizeof(rtable_pair), cmprtable);
  unsigned int width = (1LL << 32) / VOL_HASH_TABLE_SIZE;
  unsigned int pos; // target position to allocate
  // select vol with closest random number for each bucket
  int i = 0; // index moving through the random numbers
  for (int j = 0; j < VOL_HASH_TABLE_SIZE; j++) {
    pos = width / 2 + j * width; // position to select closest to
    while (pos > rtable[i].rval && i < (int)rtable_size - 1) {
      i++;
    }
    ttable[j] = mapping[rtable[i].idx];
    gotvol[rtable[i].idx]++;
  }
  for (int i = 0; i < num_vols; i++) {
    Debug("cache_init", "build_vol_hash_table index %d mapped to %d requested %d got %d", i, mapping[i], forvol[i], gotvol[i]);
  }
  // install new table
  if (nullptr != (old_table = ink_atomic_swap(&(cp->vol_hash_table), ttable))) {
    new_Freer(old_table, CACHE_MEM_FREE_TIMEOUT);
  }
  ats_free(mapping);
  ats_free(p);
  ats_free(forvol);
  ats_free(gotvol);
  ats_free(rnd);
  ats_free(rtable_entries);
  ats_free(rtable);
}

void
Cache::vol_initialized(bool result)
{
  if (result) {
    ink_atomic_increment(&total_good_nvol, 1);
  }
  if (total_nvol == ink_atomic_increment(&total_initialized_vol, 1) + 1) {
    open_done();
  }
}

/** Set the state of a disk programmatically.
 */
bool
CacheProcessor::mark_storage_offline(CacheDisk *d, ///< Target disk
                                     bool admin)
{
  bool zret; // indicates whether there's any online storage left.
  int p;
  uint64_t total_bytes_delete = 0;
  uint64_t total_dir_delete   = 0;
  uint64_t used_dir_delete    = 0;

  /* Don't mark it again, it will invalidate the stats! */
  if (!d->online) {
    return this->has_online_storage();
  }

  d->online = false;

  if (!DISK_BAD(d)) {
    SET_DISK_BAD(d);
  }

  for (p = 0; p < gnvol; p++) {
    if (d->fd == gvol[p]->fd) {
      total_dir_delete += gvol[p]->buckets * gvol[p]->segments * DIR_DEPTH;
      used_dir_delete += dir_entries_used(gvol[p]);
      total_bytes_delete += gvol[p]->len - gvol[p]->dirlen();
    }
  }

  RecIncrGlobalRawStat(cache_rsb, cache_bytes_total_stat, -total_bytes_delete);
  RecIncrGlobalRawStat(cache_rsb, cache_direntries_total_stat, -total_dir_delete);
  RecIncrGlobalRawStat(cache_rsb, cache_direntries_used_stat, -used_dir_delete);

  /* Update the span metrics, if failing then move the span from "failing" to "offline" bucket
   * if operator took it offline, move it from "online" to "offline" bucket */
  RecIncrGlobalRawStat(cache_rsb, admin ? cache_span_online_stat : cache_span_failing_stat, -1);
  RecIncrGlobalRawStat(cache_rsb, cache_span_offline_stat, 1);

  if (theCache) {
    rebuild_host_table(theCache);
  }

  zret = this->has_online_storage();
  if (!zret) {
    Warning("All storage devices offline, cache disabled");
    CacheProcessor::cache_ready = 0;
  } else { // check cache types specifically
    if (theCache && !theCache->hosttable->gen_host_rec.vol_hash_table) {
      unsigned int caches_ready = 0;
      caches_ready              = caches_ready | (1 << CACHE_FRAG_TYPE_HTTP);
      caches_ready              = caches_ready | (1 << CACHE_FRAG_TYPE_NONE);
      caches_ready              = ~caches_ready;
      CacheProcessor::cache_ready &= caches_ready;
      Warning("all volumes for http cache are corrupt, http cache disabled");
    }
  }

  return zret;
}

bool
CacheProcessor::has_online_storage() const
{
  CacheDisk **dptr = gdisks;
  for (int disk_no = 0; disk_no < gndisks; ++disk_no, ++dptr) {
    if (!DISK_BAD(*dptr) && (*dptr)->online) {
      return true;
    }
  }
  return false;
}

int
AIO_Callback_handler::handle_disk_failure(int /* event ATS_UNUSED */, void *data)
{
  /* search for the matching file descriptor */
  if (!CacheProcessor::cache_ready) {
    return EVENT_DONE;
  }
  int disk_no     = 0;
  AIOCallback *cb = (AIOCallback *)data;

  for (; disk_no < gndisks; disk_no++) {
    CacheDisk *d = gdisks[disk_no];

    if (d->fd == cb->aiocb.aio_fildes) {
      char message[256];
      d->incrErrors(cb);

      if (!DISK_BAD(d)) {
        snprintf(message, sizeof(message), "Error accessing Disk %s [%d/%d]", d->path, d->num_errors, cache_config_max_disk_errors);
        Warning("%s", message);
        RecSignalManager(REC_SIGNAL_CACHE_WARNING, message);
      } else if (!DISK_BAD_SIGNALLED(d)) {
        snprintf(message, sizeof(message), "too many errors accessing disk %s [%d/%d]: declaring disk bad", d->path, d->num_errors,
                 cache_config_max_disk_errors);
        Warning("%s", message);
        RecSignalManager(REC_SIGNAL_CACHE_ERROR, message);
        cacheProcessor.mark_storage_offline(d); // take it out of service
      }
      break;
    }
  }

  delete cb;
  return EVENT_DONE;
}

int
Cache::open_done()
{
  Action *register_ShowCache(Continuation * c, HTTPHdr * h);
  Action *register_ShowCacheInternal(Continuation * c, HTTPHdr * h);
  statPagesManager.register_http("cache", register_ShowCache);
  statPagesManager.register_http("cache-internal", register_ShowCacheInternal);

  if (total_good_nvol == 0) {
    ready = CACHE_INIT_FAILED;
    cacheProcessor.cacheInitialized();
    return 0;
  }

  hosttable = new CacheHostTable(this, scheme);
  hosttable->register_config_callback(&hosttable);

  if (hosttable->gen_host_rec.num_cachevols == 0) {
    ready = CACHE_INIT_FAILED;
  } else {
    ready = CACHE_INITIALIZED;
  }

  // TS-3848
  if (ready == CACHE_INIT_FAILED && cacheProcessor.waitForCache() >= 2) {
    Fatal("Failed to initialize cache host table");
  }

  cacheProcessor.cacheInitialized();

  return 0;
}

int
Cache::open(bool clear, bool /* fix ATS_UNUSED */)
{
  int i;
  off_t blocks          = 0;
  cache_read_done       = 0;
  total_initialized_vol = 0;
  total_nvol            = 0;
  total_good_nvol       = 0;

  REC_EstablishStaticConfigInt32(cache_config_min_average_object_size, "proxy.config.cache.min_average_object_size");
  Debug("cache_init", "Cache::open - proxy.config.cache.min_average_object_size = %d", (int)cache_config_min_average_object_size);

  CacheVol *cp = cp_list.head;
  for (; cp; cp = cp->link.next) {
    if (cp->scheme == scheme) {
      cp->vols   = (Vol **)ats_malloc(cp->num_vols * sizeof(Vol *));
      int vol_no = 0;
      for (i = 0; i < gndisks; i++) {
        if (cp->disk_vols[i] && !DISK_BAD(cp->disk_vols[i]->disk)) {
          DiskVolBlockQueue *q = cp->disk_vols[i]->dpb_queue.head;
          for (; q; q = q->link.next) {
            cp->vols[vol_no]            = new Vol();
            CacheDisk *d                = cp->disk_vols[i]->disk;
            cp->vols[vol_no]->disk      = d;
            cp->vols[vol_no]->fd        = d->fd;
            cp->vols[vol_no]->cache     = this;
            cp->vols[vol_no]->cache_vol = cp;
            blocks                      = q->b->len;

            bool vol_clear = clear || d->cleared || q->new_block;
#if AIO_MODE == AIO_MODE_NATIVE
            eventProcessor.schedule_imm(new VolInit(cp->vols[vol_no], d->path, blocks, q->b->offset, vol_clear));
#else
            cp->vols[vol_no]->init(d->path, blocks, q->b->offset, vol_clear);
#endif
            vol_no++;
            cache_size += blocks;
          }
        }
      }
      total_nvol += vol_no;
    }
  }
  if (total_nvol == 0) {
    return open_done();
  }
  cache_read_done = 1;
  return 0;
}

int
Cache::close()
{
  return -1;
}

int
CacheVC::dead(int /* event ATS_UNUSED */, Event * /*e ATS_UNUSED */)
{
  ink_assert(0);
  return EVENT_DONE;
}

bool
CacheVC::is_pread_capable()
{
  return !f.read_from_writer_called;
}

#define STORE_COLLISION 1

static void
unmarshal_helper(Doc *doc, Ptr<IOBufferData> &buf, int &okay)
{
  using UnmarshalFunc           = int(char *buf, int len, RefCountObj *block_ref);
  UnmarshalFunc *unmarshal_func = &HTTPInfo::unmarshal;
  ts::VersionNumber version(doc->v_major, doc->v_minor);

  // introduced by https://github.com/apache/trafficserver/pull/4874, this is used to distinguish the doc version
  // before and after #4847
  if (version < CACHE_DB_VERSION) {
    unmarshal_func = &HTTPInfo::unmarshal_v24_1;
  }

  char *tmp = doc->hdr();
  int len   = doc->hlen;
  while (len > 0) {
    int r = unmarshal_func(tmp, len, buf.get());
    if (r < 0) {
      ink_assert(!"CacheVC::handleReadDone unmarshal failed");
      okay = 0;
      break;
    }
    len -= r;
    tmp += r;
  }
}

/** Upgrade a marshalled fragment buffer to the current version.

    @internal I looked at doing this in place (rather than a copy & modify) but
    - The in place logic would be even worse than this mess
    - It wouldn't save you that much, since you end up doing inserts early in the buffer.
      Without extreme care in the logic it could end up doing more copying thatn
      the simpler copy & modify.

    @internal This logic presumes the existence of some slack at the end of the buffer, which
    is usually the case (because lots of rounding is done). If there isn't enough slack then
    this fails and we have a cache miss. The assumption that this is sufficiently rare that
    code simplicity takes precedence should be checked at some point.
 */
static bool
upgrade_doc_version(Ptr<IOBufferData> &buf)
{
  // Type definition is close enough to use for initial checking.
  cache_bc::Doc_v23 *doc = reinterpret_cast<cache_bc::Doc_v23 *>(buf->data());
  bool zret              = true;

  if (DOC_MAGIC == doc->magic) {
    if (0 == doc->hlen) {
      Debug("cache_bc", "Doc %p without header, no upgrade needed.", doc);
    } else if (CACHE_FRAG_TYPE_HTTP_V23 == doc->doc_type) {
      cache_bc::HTTPCacheAlt_v21 *alt = reinterpret_cast<cache_bc::HTTPCacheAlt_v21 *>(doc->hdr());
      if (alt && alt->is_unmarshalled_format()) {
        Ptr<IOBufferData> d_buf(ioDataAllocator.alloc());
        Doc *d_doc;
        char *src;
        char *dst;
        char *hdr_limit = doc->data();
        HTTPInfo::FragOffset *frags =
          reinterpret_cast<HTTPInfo::FragOffset *>(static_cast<char *>(buf->data()) + sizeof(cache_bc::Doc_v23));
        int frag_count      = doc->_flen / sizeof(HTTPInfo::FragOffset);
        size_t n            = 0;
        size_t content_size = doc->data_len();

        Debug("cache_bc", "Doc %p is 3.2", doc);

        // Use the same buffer size, fail if no fit.
        d_buf->alloc(buf->_size_index, buf->_mem_type); // Duplicate.
        d_doc = reinterpret_cast<Doc *>(d_buf->data());
        n     = d_buf->block_size();

        src = buf->data();
        dst = d_buf->data();
        memcpy(dst, src, sizeof(Doc));
        src += sizeof(Doc) + doc->_flen;
        dst += sizeof(Doc);
        n -= sizeof(Doc);

        // We copy the fragment table iff there is a fragment table and there is only one alternate.
        if (frag_count > 0 && cache_bc::HTTPInfo_v21::marshalled_length(src) > doc->hlen) {
          frag_count = 0; // inhibit fragment table insertion.
        }

        while (zret && src < hdr_limit) {
          zret = cache_bc::HTTPInfo_v21::copy_and_upgrade_unmarshalled_to_v23(dst, src, n, frag_count, frags);
        }
        if (zret && content_size <= n) {
          memcpy(dst, src, content_size); // content
          // Must update new Doc::len and Doc::hlen
          // dst points at the first byte of the content, or one past the last byte of the alt header.
          d_doc->len  = (dst - reinterpret_cast<char *>(d_doc)) + content_size;
          d_doc->hlen = (dst - reinterpret_cast<char *>(d_doc)) - sizeof(Doc);
          buf         = d_buf; // replace original buffer with new buffer.
        } else {
          zret = false;
        }
      }
      Doc *n_doc = reinterpret_cast<Doc *>(buf->data()); // access as current version.
      // For now the base header size is the same. If that changes we'll need to handle the v22/23 case here
      // as with the v21 and shift the content down to accomodate the bigger header.
      ink_assert(sizeof(*n_doc) == sizeof(*doc));

      n_doc->doc_type = CACHE_FRAG_TYPE_HTTP; // We converted so adjust doc_type.
      // Set these to zero for debugging - they'll be updated to the current values if/when this is
      // put in the aggregation buffer.
      n_doc->v_major = 0;
      n_doc->v_minor = 0;
      n_doc->unused  = 0; // force to zero to make future use easier.
    }
  }
  return zret;
}

// [amc] I think this is where all disk reads from cache funnel through here.
int
CacheVC::handleReadDone(int event, Event *e)
{
  cancel_trigger();
  ink_assert(this_ethread() == mutex->thread_holding);

  Doc *doc = nullptr;
  if (event == AIO_EVENT_DONE) {
    set_io_not_in_progress();
  } else if (is_io_in_progress()) {
    return EVENT_CONT;
  }
  {
    MUTEX_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if ((!dir_valid(vol, &dir)) || (!io.ok())) {
      if (!io.ok()) {
        Debug("cache_disk_error", "Read error on disk %s\n \
	    read range : [%" PRIu64 " - %" PRIu64 " bytes]  [%" PRIu64 " - %" PRIu64 " blocks] \n",
              vol->hash_text.get(), (uint64_t)io.aiocb.aio_offset, (uint64_t)io.aiocb.aio_offset + io.aiocb.aio_nbytes,
              (uint64_t)io.aiocb.aio_offset / 512, (uint64_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) / 512);
      }
      goto Ldone;
    }

    doc = reinterpret_cast<Doc *>(buf->data());
    ink_assert(vol->mutex->nthread_holding < 1000);
    ink_assert(doc->magic == DOC_MAGIC);

    /* We've read the raw data from disk, time to deserialize it. We have to account for a variety of formats that
       may be present.

       As of cache version 24 we changed the @a doc_type to indicate a format change in the header which includes
       version data inside the header. Prior to that we must use heuristics to deduce the actual format. For this reason
       we send that header type off for special processing. Otherwise we can use the in object version to determine
       the format.

       All of this processing must produce a serialized header that is compliant with the current version. This includes
       updating the doc_type.
    */

    if (doc->doc_type == CACHE_FRAG_TYPE_HTTP_V23) {
      if (upgrade_doc_version(buf)) {
        doc = reinterpret_cast<Doc *>(buf->data()); // buf may be a new copy
      } else {
        Debug("cache_bc", "Upgrade of fragment failed - disk %s - doc id = %" PRIx64 ":%" PRIx64 "", vol->hash_text.get(),
              read_key->slice64(0), read_key->slice64(1));
        doc->magic = DOC_CORRUPT;
        // Should really trash the directory entry for this, as it's never going to work in the future.
        // Or does that happen later anyway?
        goto Ldone;
      }
    } else if (doc->doc_type == CACHE_FRAG_TYPE_HTTP) { // handle any version updates based on the object version
      if (ts::VersionNumber(doc->v_major, doc->v_minor) > CACHE_DB_VERSION) {
        // future version, count as corrupted
        doc->magic = DOC_CORRUPT;
        Debug("cache_bc", "Object is future version %d:%d - disk %s - doc id = %" PRIx64 ":%" PRIx64 "", doc->v_major, doc->v_minor,
              vol->hash_text.get(), read_key->slice64(0), read_key->slice64(1));
        goto Ldone;
      }
    }

#ifdef VERIFY_JTEST_DATA
    char xx[500];
    if (read_key && *read_key == doc->key && request.valid() && !dir_head(&dir) && !vio.ndone) {
      int ib = 0, xd = 0;
      request.url_get()->print(xx, 500, &ib, &xd);
      char *x = xx;
      for (int q = 0; q < 3; q++)
        x = strchr(x + 1, '/');
      ink_assert(!memcmp(doc->data(), x, ib - (x - xx)));
    }
#endif

    if (is_debug_tag_set("cache_read")) {
      char xt[CRYPTO_HEX_SIZE];
      Debug("cache_read", "Read complete on fragment %s. Length: data payload=%d this fragment=%d total doc=%" PRId64 " prefix=%d",
            doc->key.toHexStr(xt), doc->data_len(), doc->len, doc->total_len, doc->prefix_len());
    }

    // put into ram cache?
    if (io.ok() && ((doc->first_key == *read_key) || (doc->key == *read_key) || STORE_COLLISION) && doc->magic == DOC_MAGIC) {
      int okay = 1;
      if (!f.doc_from_ram_cache) {
        f.not_from_ram_cache = 1;
      }
      if (cache_config_enable_checksum && doc->checksum != DOC_NO_CHECKSUM) {
        // verify that the checksum matches
        uint32_t checksum = 0;
        for (char *b = doc->hdr(); b < (char *)doc + doc->len; b++) {
          checksum += *b;
        }
        ink_assert(checksum == doc->checksum);
        if (checksum != doc->checksum) {
          Note("cache: checksum error for [%" PRIu64 " %" PRIu64 "] len %d, hlen %d, disk %s, offset %" PRIu64 " size %zu",
               doc->first_key.b[0], doc->first_key.b[1], doc->len, doc->hlen, vol->path, (uint64_t)io.aiocb.aio_offset,
               (size_t)io.aiocb.aio_nbytes);
          doc->magic = DOC_CORRUPT;
          okay       = 0;
        }
      }
      (void)e; // Avoid compiler warnings
      bool http_copy_hdr = false;
      http_copy_hdr =
        cache_config_ram_cache_compress && !f.doc_from_ram_cache && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen;
      // If http doc we need to unmarshal the headers before putting in the ram cache
      // unless it could be compressed
      if (!http_copy_hdr && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen && okay) {
        unmarshal_helper(doc, buf, okay);
      }
      // Put the request in the ram cache only if its a open_read or lookup
      if (vio.op == VIO::READ && okay) {
        bool cutoff_check;
        // cutoff_check :
        // doc_len == 0 for the first fragment (it is set from the vector)
        //                The decision on the first fragment is based on
        //                doc->total_len
        // After that, the decision is based of doc_len (doc_len != 0)
        // (cache_config_ram_cache_cutoff == 0) : no cutoffs
        cutoff_check = ((!doc_len && (int64_t)doc->total_len < cache_config_ram_cache_cutoff) ||
                        (doc_len && (int64_t)doc_len < cache_config_ram_cache_cutoff) || !cache_config_ram_cache_cutoff);
        if (cutoff_check && !f.doc_from_ram_cache) {
          uint64_t o = dir_offset(&dir);
          vol->ram_cache->put(read_key, buf.get(), doc->len, http_copy_hdr, (uint32_t)(o >> 32), (uint32_t)o);
        }
        if (!doc_len) {
          // keep a pointer to it. In case the state machine decides to
          // update this document, we don't have to read it back in memory
          // again
          vol->first_fragment_key    = *read_key;
          vol->first_fragment_offset = dir_offset(&dir);
          vol->first_fragment_data   = buf;
        }
      } // end VIO::READ check
      // If it could be compressed, unmarshal after
      if (http_copy_hdr && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen && okay) {
        unmarshal_helper(doc, buf, okay);
      }
    } // end io.ok() check
  }
Ldone:
  POP_HANDLER;
  return handleEvent(AIO_EVENT_DONE, nullptr);
}

int
CacheVC::handleRead(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();

  f.doc_from_ram_cache = false;

  // check ram cache
  ink_assert(vol->mutex->thread_holding == this_ethread());
  int64_t o           = dir_offset(&dir);
  int ram_hit_state   = vol->ram_cache->get(read_key, &buf, (uint32_t)(o >> 32), (uint32_t)o);
  f.compressed_in_ram = (ram_hit_state > RAM_HIT_COMPRESS_NONE) ? 1 : 0;
  if (ram_hit_state >= RAM_HIT_COMPRESS_NONE) {
    goto LramHit;
  }

  // check if it was read in the last open_read call
  if (*read_key == vol->first_fragment_key && dir_offset(&dir) == vol->first_fragment_offset) {
    buf = vol->first_fragment_data;
    goto LmemHit;
  }
  // see if its in the aggregation buffer
  if (dir_agg_buf_valid(vol, &dir)) {
    int agg_offset = vol->vol_offset(&dir) - vol->header->write_pos;
    buf            = new_IOBufferData(iobuffer_size_to_index(io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
    ink_assert((agg_offset + io.aiocb.aio_nbytes) <= (unsigned)vol->agg_buf_pos);
    char *doc = buf->data();
    char *agg = vol->agg_buffer + agg_offset;
    memcpy(doc, agg, io.aiocb.aio_nbytes);
    io.aio_result = io.aiocb.aio_nbytes;
    SET_HANDLER(&CacheVC::handleReadDone);
    return EVENT_RETURN;
  }

  io.aiocb.aio_fildes = vol->fd;
  io.aiocb.aio_offset = vol->vol_offset(&dir);
  if ((off_t)(io.aiocb.aio_offset + io.aiocb.aio_nbytes) > (off_t)(vol->skip + vol->len)) {
    io.aiocb.aio_nbytes = vol->skip + vol->len - io.aiocb.aio_offset;
  }
  buf              = new_IOBufferData(iobuffer_size_to_index(io.aiocb.aio_nbytes, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
  io.aiocb.aio_buf = buf->data();
  io.action        = this;
  io.thread        = mutex->thread_holding->tt == DEDICATED ? AIO_CALLBACK_THREAD_ANY : mutex->thread_holding;
  SET_HANDLER(&CacheVC::handleReadDone);
  ink_assert(ink_aio_read(&io) >= 0);
  CACHE_DEBUG_INCREMENT_DYN_STAT(cache_pread_count_stat);
  return EVENT_CONT;

LramHit : {
  f.doc_from_ram_cache = true;
  io.aio_result        = io.aiocb.aio_nbytes;
  Doc *doc             = (Doc *)buf->data();
  if (cache_config_ram_cache_compress && doc->doc_type == CACHE_FRAG_TYPE_HTTP && doc->hlen) {
    SET_HANDLER(&CacheVC::handleReadDone);
    return EVENT_RETURN;
  }
}
LmemHit:
  f.doc_from_ram_cache = true;
  io.aio_result        = io.aiocb.aio_nbytes;
  POP_HANDLER;
  return EVENT_RETURN; // allow the caller to release the volume lock
}

Action *
Cache::lookup(Continuation *cont, const CacheKey *key, CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_LOOKUP_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  Vol *vol          = key_to_vol(key, hostname, host_len);
  ProxyMutex *mutex = cont->mutex.get();
  CacheVC *c        = new_CacheVC(cont);
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
  c->vio.op    = VIO::READ;
  c->base_stat = cache_lookup_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->first_key = c->key = *key;
  c->frag_type          = type;
  c->f.lookup           = 1;
  c->vol                = vol;
  c->last_collision     = nullptr;

  if (c->handleEvent(EVENT_INTERVAL, nullptr) == EVENT_CONT) {
    return &c->_action;
  } else {
    return ACTION_RESULT_DONE;
  }
}

int
CacheVC::removeEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  cancel_trigger();
  set_io_not_in_progress();
  {
    MUTEX_TRY_LOCK(lock, vol->mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      VC_SCHED_LOCK_RETRY();
    }
    if (_action.cancelled) {
      if (od) {
        vol->close_write(this);
        od = nullptr;
      }
      goto Lfree;
    }
    if (!f.remove_aborted_writers) {
      if (vol->open_write(this, true, 1)) {
        // writer  exists
        od = vol->open_read(&key);
        ink_release_assert(od);
        od->dont_update_directory = true;
        od                        = nullptr;
      } else {
        od->dont_update_directory = true;
      }
      f.remove_aborted_writers = 1;
    }
  Lread:
    SET_HANDLER(&CacheVC::removeEvent);
    if (!buf) {
      goto Lcollision;
    }
    if (!dir_valid(vol, &dir)) {
      last_collision = nullptr;
      goto Lcollision;
    }
    // check read completed correct FIXME: remove bad vols
    if ((size_t)io.aio_result != (size_t)io.aiocb.aio_nbytes) {
      goto Ldone;
    }
    {
      // verify that this is our document
      Doc *doc = (Doc *)buf->data();
      /* should be first_key not key..right?? */
      if (doc->first_key == key) {
        ink_assert(doc->magic == DOC_MAGIC);
        if (dir_delete(&key, vol, &dir) > 0) {
          if (od) {
            vol->close_write(this);
          }
          od = nullptr;
          goto Lremoved;
        }
        goto Ldone;
      }
    }
  Lcollision:
    // check for collision
    if (dir_probe(&key, vol, &dir, &last_collision) > 0) {
      int ret = do_read_call(&key);
      if (ret == EVENT_RETURN) {
        goto Lread;
      }
      return ret;
    }
  Ldone:
    CACHE_INCREMENT_DYN_STAT(cache_remove_failure_stat);
    if (od) {
      vol->close_write(this);
    }
  }
  ink_assert(!vol || this_ethread() != vol->mutex->thread_holding);
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE_FAILED, (void *)-ECACHE_NO_DOC);
  goto Lfree;
Lremoved:
  _action.continuation->handleEvent(CACHE_EVENT_REMOVE, nullptr);
Lfree:
  return free_CacheVC(this);
}

Action *
Cache::remove(Continuation *cont, const CacheKey *key, CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    if (cont) {
      cont->handleEvent(CACHE_EVENT_REMOVE_FAILED, nullptr);
    }
    return ACTION_RESULT_DONE;
  }

  Ptr<ProxyMutex> mutex;
  if (!cont) {
    cont = new_CacheRemoveCont();
  }

  CACHE_TRY_LOCK(lock, cont->mutex, this_ethread());
  ink_assert(lock.is_locked());
  Vol *vol = key_to_vol(key, hostname, host_len);
  // coverity[var_decl]
  Dir result;
  dir_clear(&result); // initialized here, set result empty so we can recognize missed lock
  mutex = cont->mutex;

  CacheVC *c   = new_CacheVC(cont);
  c->vio.op    = VIO::NONE;
  c->frag_type = type;
  c->base_stat = cache_remove_active_stat;
  CACHE_INCREMENT_DYN_STAT(c->base_stat + CACHE_STAT_ACTIVE);
  c->first_key = c->key = *key;
  c->vol                = vol;
  c->dir                = result;
  c->f.remove           = 1;

  SET_CONTINUATION_HANDLER(c, &CacheVC::removeEvent);
  int ret = c->removeEvent(EVENT_IMMEDIATE, nullptr);
  if (ret == EVENT_DONE) {
    return ACTION_RESULT_DONE;
  } else {
    return &c->_action;
  }
}
// CacheVConnection

CacheVConnection::CacheVConnection() : VConnection(nullptr) {}

void
cplist_init()
{
  cp_list_len = 0;
  for (int i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    DiskVol **dp = d->disk_vols;
    for (unsigned int j = 0; j < d->header->num_volumes; j++) {
      ink_assert(dp[j]->dpb_queue.head);
      CacheVol *p = cp_list.head;
      while (p) {
        if (p->vol_number == dp[j]->vol_number) {
          ink_assert(p->scheme == (int)dp[j]->dpb_queue.head->b->type);
          p->size += dp[j]->size;
          p->num_vols += dp[j]->num_volblocks;
          p->disk_vols[i] = dp[j];
          break;
        }
        p = p->link.next;
      }
      if (!p) {
        // did not find a volume in the cache vol list...create
        // a new one
        CacheVol *new_p   = new CacheVol();
        new_p->vol_number = dp[j]->vol_number;
        new_p->num_vols   = dp[j]->num_volblocks;
        new_p->size       = dp[j]->size;
        new_p->scheme     = dp[j]->dpb_queue.head->b->type;
        new_p->disk_vols  = (DiskVol **)ats_malloc(gndisks * sizeof(DiskVol *));
        memset(new_p->disk_vols, 0, gndisks * sizeof(DiskVol *));
        new_p->disk_vols[i] = dp[j];
        cp_list.enqueue(new_p);
        cp_list_len++;
      }
    }
  }
}

void
cplist_update()
{
  /* go through cplist and delete volumes that are not in the volume.config */
  CacheVol *cp = cp_list.head;
  ConfigVol *config_vol;

  while (cp) {
    for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
      if (config_vol->number == cp->vol_number) {
        if (cp->scheme == config_vol->scheme) {
          config_vol->cachep = cp;
        } else {
          /* delete this volume from all the disks */
          int d_no;
          int clearCV = 1;

          for (d_no = 0; d_no < gndisks; d_no++) {
            if (cp->disk_vols[d_no]) {
              if (cp->disk_vols[d_no]->disk->forced_volume_num == cp->vol_number) {
                clearCV            = 0;
                config_vol->cachep = cp;
              } else {
                cp->disk_vols[d_no]->disk->delete_volume(cp->vol_number);
                cp->disk_vols[d_no] = nullptr;
              }
            }
          }
          if (clearCV) {
            config_vol = nullptr;
          }
        }
        break;
      }
    }

    if (!config_vol) {
      // did not find a matching volume in the config file.
      // Delete hte volume from the cache vol list
      int d_no;
      for (d_no = 0; d_no < gndisks; d_no++) {
        if (cp->disk_vols[d_no]) {
          cp->disk_vols[d_no]->disk->delete_volume(cp->vol_number);
        }
      }
      CacheVol *temp_cp = cp;
      cp                = cp->link.next;
      cp_list.remove(temp_cp);
      cp_list_len--;
      delete temp_cp;
      continue;
    } else {
      cp = cp->link.next;
    }
  }
}

static int
fillExclusiveDisks(CacheVol *cp)
{
  int diskCount     = 0;
  int volume_number = cp->vol_number;

  Debug("cache_init", "volume %d", volume_number);
  for (int i = 0; i < gndisks; i++) {
    if (gdisks[i]->forced_volume_num != volume_number) {
      continue;
    }
    /* The user had created several volumes before - clear the disk
       and create one volume for http */
    for (int j = 0; j < (int)gdisks[i]->header->num_volumes; j++) {
      if (volume_number != gdisks[i]->disk_vols[j]->vol_number) {
        Note("Clearing Disk: %s", gdisks[i]->path);
        gdisks[i]->delete_all_volumes();
        break;
      }
    }
    diskCount++;

    int64_t size_diff = gdisks[i]->num_usable_blocks;
    DiskVolBlock *dpb;

    do {
      dpb = gdisks[i]->create_volume(volume_number, size_diff, cp->scheme);
      if (dpb) {
        if (!cp->disk_vols[i]) {
          cp->disk_vols[i] = gdisks[i]->get_diskvol(volume_number);
        }
        size_diff -= dpb->len;
        cp->size += dpb->len;
        cp->num_vols++;
      } else {
        Debug("cache_init", "create_volume failed");
        break;
      }
    } while ((size_diff > 0));
  }
  return diskCount;
}

int
cplist_reconfigure()
{
  int64_t size;
  int volume_number;
  off_t size_in_blocks;
  ConfigVol *config_vol;

  gnvol = 0;
  if (config_volumes.num_volumes == 0) {
    /* only the http cache */
    CacheVol *cp   = new CacheVol();
    cp->vol_number = 0;
    cp->scheme     = CACHE_HTTP_TYPE;
    cp->disk_vols  = (DiskVol **)ats_malloc(gndisks * sizeof(DiskVol *));
    memset(cp->disk_vols, 0, gndisks * sizeof(DiskVol *));
    cp_list.enqueue(cp);
    cp_list_len++;
    for (int i = 0; i < gndisks; i++) {
      if (gdisks[i]->header->num_volumes != 1 || gdisks[i]->disk_vols[0]->vol_number != 0) {
        /* The user had created several volumes before - clear the disk
           and create one volume for http */
        Note("Clearing Disk: %s", gdisks[i]->path);
        gdisks[i]->delete_all_volumes();
      }
      if (gdisks[i]->cleared) {
        uint64_t free_space = gdisks[i]->free_space * STORE_BLOCK_SIZE;
        int vols            = (free_space / MAX_VOL_SIZE) + 1;
        for (int p = 0; p < vols; p++) {
          off_t b = gdisks[i]->free_space / (vols - p);
          Debug("cache_hosting", "blocks = %" PRId64, (int64_t)b);
          DiskVolBlock *dpb = gdisks[i]->create_volume(0, b, CACHE_HTTP_TYPE);
          ink_assert(dpb && dpb->len == (uint64_t)b);
        }
        ink_assert(gdisks[i]->free_space == 0);
      }

      ink_assert(gdisks[i]->header->num_volumes == 1);
      DiskVol **dp = gdisks[i]->disk_vols;
      gnvol += dp[0]->num_volblocks;
      cp->size += dp[0]->size;
      cp->num_vols += dp[0]->num_volblocks;
      cp->disk_vols[i] = dp[0];
    }

  } else {
    for (int i = 0; i < gndisks; i++) {
      if (gdisks[i]->header->num_volumes == 1 && gdisks[i]->disk_vols[0]->vol_number == 0) {
        /* The user had created several volumes before - clear the disk
           and create one volume for http */
        Note("Clearing Disk: %s", gdisks[i]->path);
        gdisks[i]->delete_all_volumes();
      }
    }

    /* change percentages in the config patitions to absolute value */
    off_t tot_space_in_blks = 0;
    off_t blocks_per_vol    = VOL_BLOCK_SIZE / STORE_BLOCK_SIZE;
    /* sum up the total space available on all the disks.
       round down the space to 128 megabytes */
    for (int i = 0; i < gndisks; i++) {
      tot_space_in_blks += (gdisks[i]->num_usable_blocks / blocks_per_vol) * blocks_per_vol;
    }

    double percent_remaining = 100.00;
    for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
      if (config_vol->in_percent) {
        if (config_vol->percent > percent_remaining) {
          Warning("total volume sizes added up to more than 100%%!");
          Warning("no volumes created");
          return -1;
        }
        int64_t space_in_blks = (int64_t)(((double)(config_vol->percent / percent_remaining)) * tot_space_in_blks);

        space_in_blks = space_in_blks >> (20 - STORE_BLOCK_SHIFT);
        /* round down to 128 megabyte multiple */
        space_in_blks    = (space_in_blks >> 7) << 7;
        config_vol->size = space_in_blks;
        tot_space_in_blks -= space_in_blks << (20 - STORE_BLOCK_SHIFT);
        percent_remaining -= (config_vol->size < 128) ? 0 : config_vol->percent;
      }
      if (config_vol->size < 128) {
        Warning("the size of volume %d (%" PRId64 ") is less than the minimum required volume size %d", config_vol->number,
                (int64_t)config_vol->size, 128);
        Warning("volume %d is not created", config_vol->number);
      }
      Debug("cache_hosting", "Volume: %d Size: %" PRId64, config_vol->number, (int64_t)config_vol->size);
    }
    cplist_update();
    /* go through volume config and grow and create volumes */

    for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
      // if volume is given exclusive disks, fill here and continue
      if (!config_vol->cachep) {
        continue;
      }
      fillExclusiveDisks(config_vol->cachep);
    }

    for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
      size = config_vol->size;
      if (size < 128) {
        continue;
      }

      volume_number = config_vol->number;

      size_in_blocks = ((off_t)size * 1024 * 1024) / STORE_BLOCK_SIZE;

      if (config_vol->cachep && config_vol->cachep->num_vols > 0) {
        gnvol += config_vol->cachep->num_vols;
        continue;
      }

      if (!config_vol->cachep) {
        // we did not find a corresponding entry in cache vol...creat one

        CacheVol *new_cp  = new CacheVol();
        new_cp->disk_vols = (DiskVol **)ats_malloc(gndisks * sizeof(DiskVol *));
        memset(new_cp->disk_vols, 0, gndisks * sizeof(DiskVol *));
        if (create_volume(config_vol->number, size_in_blocks, config_vol->scheme, new_cp)) {
          ats_free(new_cp->disk_vols);
          new_cp->disk_vols = nullptr;
          delete new_cp;
          return -1;
        }
        cp_list.enqueue(new_cp);
        cp_list_len++;
        config_vol->cachep = new_cp;
        gnvol += new_cp->num_vols;
        continue;
      }
      //    else
      CacheVol *cp = config_vol->cachep;
      ink_assert(cp->size <= size_in_blocks);
      if (cp->size == size_in_blocks) {
        gnvol += cp->num_vols;
        continue;
      }
      // else the size is greater...
      /* search the cp_list */

      int *sorted_vols = new int[gndisks];
      for (int i = 0; i < gndisks; i++) {
        sorted_vols[i] = i;
      }
      for (int i = 0; i < gndisks - 1; i++) {
        int smallest     = sorted_vols[i];
        int smallest_ndx = i;
        for (int j = i + 1; j < gndisks; j++) {
          int curr      = sorted_vols[j];
          DiskVol *dvol = cp->disk_vols[curr];
          if (gdisks[curr]->cleared) {
            ink_assert(!dvol);
            // disks that are cleared should be filled first
            smallest     = curr;
            smallest_ndx = j;
          } else if (!dvol && cp->disk_vols[smallest]) {
            smallest     = curr;
            smallest_ndx = j;
          } else if (dvol && cp->disk_vols[smallest] && (dvol->size < cp->disk_vols[smallest]->size)) {
            smallest     = curr;
            smallest_ndx = j;
          }
        }
        sorted_vols[smallest_ndx] = sorted_vols[i];
        sorted_vols[i]            = smallest;
      }

      int64_t size_to_alloc = size_in_blocks - cp->size;
      int disk_full         = 0;
      for (int i = 0; (i < gndisks) && size_to_alloc; i++) {
        int disk_no = sorted_vols[i];
        ink_assert(cp->disk_vols[sorted_vols[gndisks - 1]]);
        int largest_vol = cp->disk_vols[sorted_vols[gndisks - 1]]->size;

        /* allocate storage on new disk. Find the difference
           between the biggest volume on any disk and
           the volume on this disk and try to make
           them equal */
        int64_t size_diff = (cp->disk_vols[disk_no]) ? largest_vol - cp->disk_vols[disk_no]->size : largest_vol;
        size_diff         = (size_diff < size_to_alloc) ? size_diff : size_to_alloc;
        /* if size_diff == 0, then then the disks have volumes of the
           same sizes, so we don't need to balance the disks */
        if (size_diff == 0) {
          break;
        }

        DiskVolBlock *dpb;
        do {
          dpb = gdisks[disk_no]->create_volume(volume_number, size_diff, cp->scheme);
          if (dpb) {
            if (!cp->disk_vols[disk_no]) {
              cp->disk_vols[disk_no] = gdisks[disk_no]->get_diskvol(volume_number);
            }
            size_diff -= dpb->len;
            cp->size += dpb->len;
            cp->num_vols++;
          } else {
            break;
          }
        } while ((size_diff > 0));

        if (!dpb) {
          disk_full++;
        }

        size_to_alloc = size_in_blocks - cp->size;
      }

      delete[] sorted_vols;

      if (size_to_alloc) {
        if (create_volume(volume_number, size_to_alloc, cp->scheme, cp)) {
          return -1;
        }
      }
      gnvol += cp->num_vols;
    }
  }
  return 0;
}

// This is some really bad code, and needs to be rewritten!
int
create_volume(int volume_number, off_t size_in_blocks, int scheme, CacheVol *cp)
{
  static int curr_vol  = 0; // FIXME: this will not reinitialize correctly
  off_t to_create      = size_in_blocks;
  off_t blocks_per_vol = VOL_BLOCK_SIZE >> STORE_BLOCK_SHIFT;
  int full_disks       = 0;

  cp->vol_number = volume_number;
  cp->scheme     = scheme;
  if (fillExclusiveDisks(cp)) {
    Debug("cache_init", "volume successfully filled from forced disks: volume_number=%d", volume_number);
    return 0;
  }

  int *sp = new int[gndisks];
  memset(sp, 0, gndisks * sizeof(int));

  int i = curr_vol;
  while (size_in_blocks > 0) {
    if (gdisks[i]->free_space >= (sp[i] + blocks_per_vol)) {
      sp[i] += blocks_per_vol;
      size_in_blocks -= blocks_per_vol;
      full_disks = 0;
    } else {
      full_disks += 1;
      if (full_disks == gndisks) {
        char config_file[PATH_NAME_MAX];
        REC_ReadConfigString(config_file, "proxy.config.cache.volume_filename", PATH_NAME_MAX);
        if (cp->size) {
          Warning("not enough space to increase volume: [%d] to size: [%" PRId64 "]", volume_number,
                  (int64_t)((to_create + cp->size) >> (20 - STORE_BLOCK_SHIFT)));
        } else {
          Warning("not enough space to create volume: [%d], size: [%" PRId64 "]", volume_number,
                  (int64_t)(to_create >> (20 - STORE_BLOCK_SHIFT)));
        }

        Note("edit the %s file and restart traffic_server", config_file);
        delete[] sp;
        return -1;
      }
    }
    i = (i + 1) % gndisks;
  }
  cp->vol_number = volume_number;
  cp->scheme     = scheme;
  curr_vol       = i;
  for (i = 0; i < gndisks; i++) {
    if (sp[i] > 0) {
      while (sp[i] > 0) {
        DiskVolBlock *p = gdisks[i]->create_volume(volume_number, sp[i], scheme);
        ink_assert(p && (p->len >= (unsigned int)blocks_per_vol));
        sp[i] -= p->len;
        cp->num_vols++;
        cp->size += p->len;
      }
      if (!cp->disk_vols[i]) {
        cp->disk_vols[i] = gdisks[i]->get_diskvol(volume_number);
      }
    }
  }
  delete[] sp;
  return 0;
}

void
rebuild_host_table(Cache *cache)
{
  build_vol_hash_table(&cache->hosttable->gen_host_rec);
  if (cache->hosttable->m_numEntries != 0) {
    CacheHostMatcher *hm   = cache->hosttable->getHostMatcher();
    CacheHostRecord *h_rec = hm->getDataArray();
    int h_rec_len          = hm->getNumElements();
    int i;
    for (i = 0; i < h_rec_len; i++) {
      build_vol_hash_table(&h_rec[i]);
    }
  }
}

// if generic_host_rec.vols == nullptr, what do we do???
Vol *
Cache::key_to_vol(const CacheKey *key, const char *hostname, int host_len)
{
  uint32_t h                 = (key->slice32(2) >> DIR_TAG_WIDTH) % VOL_HASH_TABLE_SIZE;
  unsigned short *hash_table = hosttable->gen_host_rec.vol_hash_table;
  CacheHostRecord *host_rec  = &hosttable->gen_host_rec;

  if (hosttable->m_numEntries > 0 && host_len) {
    CacheHostResult res;
    hosttable->Match(hostname, host_len, &res);
    if (res.record) {
      unsigned short *host_hash_table = res.record->vol_hash_table;
      if (host_hash_table) {
        if (is_debug_tag_set("cache_hosting")) {
          char format_str[50];
          snprintf(format_str, sizeof(format_str), "Volume: %%xd for host: %%.%ds", host_len);
          Debug("cache_hosting", format_str, res.record, hostname);
        }
        return res.record->vols[host_hash_table[h]];
      }
    }
  }
  if (hash_table) {
    if (is_debug_tag_set("cache_hosting")) {
      char format_str[50];
      snprintf(format_str, sizeof(format_str), "Generic volume: %%xd for host: %%.%ds", host_len);
      Debug("cache_hosting", format_str, host_rec, hostname);
    }
    return host_rec->vols[hash_table[h]];
  } else {
    return host_rec->vols[0];
  }
}

static void
reg_int(const char *str, int stat, RecRawStatBlock *rsb, const char *prefix, RecRawStatSyncCb sync_cb = RecRawStatSyncSum)
{
  char stat_str[256];
  snprintf(stat_str, sizeof(stat_str), "%s.%s", prefix, str);
  RecRegisterRawStat(rsb, RECT_PROCESS, stat_str, RECD_INT, RECP_NON_PERSISTENT, stat, sync_cb);
  DOCACHE_CLEAR_DYN_STAT(stat)
}
#define REG_INT(_str, _stat) reg_int(_str, (int)_stat, rsb, prefix)

// Register Stats
void
register_cache_stats(RecRawStatBlock *rsb, const char *prefix)
{
  // Special case for this sucker, since it uses its own aggregator.
  reg_int("bytes_used", cache_bytes_used_stat, rsb, prefix, cache_stats_bytes_used_cb);

  REG_INT("bytes_total", cache_bytes_total_stat);
  REG_INT("ram_cache.total_bytes", cache_ram_cache_bytes_total_stat);
  REG_INT("ram_cache.bytes_used", cache_ram_cache_bytes_stat);
  REG_INT("ram_cache.hits", cache_ram_cache_hits_stat);
  REG_INT("ram_cache.misses", cache_ram_cache_misses_stat);
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
  REG_INT("wrap_count", cache_directory_wrap_stat);
  REG_INT("sync.count", cache_directory_sync_count_stat);
  REG_INT("sync.bytes", cache_directory_sync_bytes_stat);
  REG_INT("sync.time", cache_directory_sync_time_stat);
  REG_INT("span.errors.read", cache_span_errors_read_stat);
  REG_INT("span.errors.write", cache_span_errors_write_stat);
  REG_INT("span.failing", cache_span_failing_stat);
  REG_INT("span.offline", cache_span_offline_stat);
  REG_INT("span.online", cache_span_online_stat);
}

int
FragmentSizeUpdateCb(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data, void *cookie)
{
  if (sizeof(Doc) >= static_cast<size_t>(data.rec_int) || static_cast<size_t>(data.rec_int) - sizeof(Doc) > MAX_FRAG_SIZE) {
    Warning("The fragments size exceed the limitation, ignore: %" PRId64 ", %d", data.rec_int, cache_config_target_fragment_size);
    return 0;
  }

  cache_config_target_fragment_size = data.rec_int;
  return 0;
}

void
ink_cache_init(ts::ModuleVersion v)
{
  ink_release_assert(v.check(CACHE_MODULE_VERSION));

  cache_rsb = RecAllocateRawStatBlock((int)cache_stat_count);

  REC_EstablishStaticConfigInteger(cache_config_ram_cache_size, "proxy.config.cache.ram_cache.size");
  Debug("cache_init", "proxy.config.cache.ram_cache.size = %" PRId64 " = %" PRId64 "Mb", cache_config_ram_cache_size,
        cache_config_ram_cache_size / (1024 * 1024));

  REC_EstablishStaticConfigInt32(cache_config_ram_cache_algorithm, "proxy.config.cache.ram_cache.algorithm");
  REC_EstablishStaticConfigInt32(cache_config_ram_cache_compress, "proxy.config.cache.ram_cache.compress");
  REC_EstablishStaticConfigInt32(cache_config_ram_cache_compress_percent, "proxy.config.cache.ram_cache.compress_percent");
  REC_ReadConfigInt32(cache_config_ram_cache_use_seen_filter, "proxy.config.cache.ram_cache.use_seen_filter");

  REC_EstablishStaticConfigInt32(cache_config_http_max_alts, "proxy.config.cache.limits.http.max_alts");
  Debug("cache_init", "proxy.config.cache.limits.http.max_alts = %d", cache_config_http_max_alts);

  REC_EstablishStaticConfigInteger(cache_config_ram_cache_cutoff, "proxy.config.cache.ram_cache_cutoff");
  Debug("cache_init", "cache_config_ram_cache_cutoff = %" PRId64 " = %" PRId64 "Mb", cache_config_ram_cache_cutoff,
        cache_config_ram_cache_cutoff / (1024 * 1024));

  REC_EstablishStaticConfigInt32(cache_config_permit_pinning, "proxy.config.cache.permit.pinning");
  Debug("cache_init", "proxy.config.cache.permit.pinning = %d", cache_config_permit_pinning);

  REC_EstablishStaticConfigInt32(cache_config_dir_sync_frequency, "proxy.config.cache.dir.sync_frequency");
  Debug("cache_init", "proxy.config.cache.dir.sync_frequency = %d", cache_config_dir_sync_frequency);

  REC_EstablishStaticConfigInt32(cache_config_select_alternate, "proxy.config.cache.select_alternate");
  Debug("cache_init", "proxy.config.cache.select_alternate = %d", cache_config_select_alternate);

  REC_EstablishStaticConfigInt32(cache_config_max_doc_size, "proxy.config.cache.max_doc_size");
  Debug("cache_init", "proxy.config.cache.max_doc_size = %d = %dMb", cache_config_max_doc_size,
        cache_config_max_doc_size / (1024 * 1024));

  REC_EstablishStaticConfigInt32(cache_config_mutex_retry_delay, "proxy.config.cache.mutex_retry_delay");
  Debug("cache_init", "proxy.config.cache.mutex_retry_delay = %dms", cache_config_mutex_retry_delay);

  REC_EstablishStaticConfigInt32(cache_config_read_while_writer_max_retries, "proxy.config.cache.read_while_writer.max_retries");
  Debug("cache_init", "proxy.config.cache.read_while_writer.max_retries = %d", cache_config_read_while_writer_max_retries);

  REC_EstablishStaticConfigInt32(cache_read_while_writer_retry_delay, "proxy.config.cache.read_while_writer_retry.delay");
  Debug("cache_init", "proxy.config.cache.read_while_writer_retry.delay = %dms", cache_read_while_writer_retry_delay);

  REC_EstablishStaticConfigInt32(cache_config_hit_evacuate_percent, "proxy.config.cache.hit_evacuate_percent");
  Debug("cache_init", "proxy.config.cache.hit_evacuate_percent = %d", cache_config_hit_evacuate_percent);

  REC_EstablishStaticConfigInt32(cache_config_hit_evacuate_size_limit, "proxy.config.cache.hit_evacuate_size_limit");
  Debug("cache_init", "proxy.config.cache.hit_evacuate_size_limit = %d", cache_config_hit_evacuate_size_limit);

  REC_EstablishStaticConfigInt32(cache_config_force_sector_size, "proxy.config.cache.force_sector_size");

  ink_assert(REC_RegisterConfigUpdateFunc("proxy.config.cache.target_fragment_size", FragmentSizeUpdateCb, nullptr) !=
             REC_ERR_FAIL);
  REC_ReadConfigInt32(cache_config_target_fragment_size, "proxy.config.cache.target_fragment_size");

  if (cache_config_target_fragment_size == 0 || cache_config_target_fragment_size - sizeof(Doc) > MAX_FRAG_SIZE) {
    cache_config_target_fragment_size = DEFAULT_TARGET_FRAGMENT_SIZE;
  }

  REC_EstablishStaticConfigInt32(enable_cache_empty_http_doc, "proxy.config.http.cache.allow_empty_doc");

  REC_EstablishStaticConfigInt32(cache_config_compatibility_4_2_0_fixup, "proxy.config.cache.http.compatibility.4-2-0-fixup");

  REC_EstablishStaticConfigInt32(cache_config_max_disk_errors, "proxy.config.cache.max_disk_errors");
  Debug("cache_init", "proxy.config.cache.max_disk_errors = %d", cache_config_max_disk_errors);

  REC_EstablishStaticConfigInt32(cache_config_agg_write_backlog, "proxy.config.cache.agg_write_backlog");
  Debug("cache_init", "proxy.config.cache.agg_write_backlog = %d", cache_config_agg_write_backlog);

  REC_EstablishStaticConfigInt32(cache_config_enable_checksum, "proxy.config.cache.enable_checksum");
  Debug("cache_init", "proxy.config.cache.enable_checksum = %d", cache_config_enable_checksum);

  REC_EstablishStaticConfigInt32(cache_config_alt_rewrite_max_size, "proxy.config.cache.alt_rewrite_max_size");
  Debug("cache_init", "proxy.config.cache.alt_rewrite_max_size = %d", cache_config_alt_rewrite_max_size);

  REC_EstablishStaticConfigInt32(cache_config_read_while_writer, "proxy.config.cache.enable_read_while_writer");
  cache_config_read_while_writer = validate_rww(cache_config_read_while_writer);
  REC_RegisterConfigUpdateFunc("proxy.config.cache.enable_read_while_writer", update_cache_config, nullptr);
  Debug("cache_init", "proxy.config.cache.enable_read_while_writer = %d", cache_config_read_while_writer);

  register_cache_stats(cache_rsb, "proxy.process.cache");

  REC_ReadConfigInteger(cacheProcessor.wait_for_cache, "proxy.config.http.wait_for_cache");

  Result result = theCacheStore.read_config();
  if (result.failed()) {
    Fatal("Failed to read cache storage configuration: %s", result.message());
  }
}

//----------------------------------------------------------------------------
Action *
CacheProcessor::open_read(Continuation *cont, const HttpCacheKey *key, CacheHTTPHdr *request, OverridableHttpConfigParams *params,
                          time_t pin_in_cache, CacheFragType type)
{
  return caches[type]->open_read(cont, &key->hash, request, params, type, key->hostname, key->hostlen);
}

//----------------------------------------------------------------------------
Action *
CacheProcessor::open_write(Continuation *cont, int expected_size, const HttpCacheKey *key, CacheHTTPHdr *request,
                           CacheHTTPInfo *old_info, time_t pin_in_cache, CacheFragType type)
{
  return caches[type]->open_write(cont, &key->hash, old_info, pin_in_cache, nullptr /* key1 */, type, key->hostname, key->hostlen);
}

//----------------------------------------------------------------------------
// Note: this should not be called from from the cluster processor, or bad
// recursion could occur. This is merely a convenience wrapper.
Action *
CacheProcessor::remove(Continuation *cont, const HttpCacheKey *key, CacheFragType frag_type)
{
  return caches[frag_type]->remove(cont, &key->hash, frag_type, key->hostname, key->hostlen);
}

CacheDisk *
CacheProcessor::find_by_path(const char *path, int len)
{
  if (CACHE_INITIALIZED == initialized) {
    // If no length is passed in, assume it's null terminated.
    if (0 >= len && 0 != *path) {
      len = strlen(path);
    }

    for (int i = 0; i < gndisks; ++i) {
      if (0 == strncmp(path, gdisks[i]->path, len)) {
        return gdisks[i];
      }
    }
  }

  return nullptr;
}

// ----------------------------

namespace cache_bc
{
static size_t const HTTP_ALT_MARSHAL_SIZE = HdrHeapMarshalBlocks{ts::round_up(sizeof(HTTPCacheAlt))}; // current size.
size_t
HTTPInfo_v21::marshalled_length(void *data)
{
  size_t zret           = HdrHeapMarshalBlocks{ts::round_up(sizeof(HTTPCacheAlt_v21))};
  HTTPCacheAlt_v21 *alt = static_cast<HTTPCacheAlt_v21 *>(data);
  HdrHeap *hdr;

  hdr = reinterpret_cast<HdrHeap *>(reinterpret_cast<char *>(alt) + reinterpret_cast<uintptr_t>(alt->m_request_hdr.m_heap));
  zret += HdrHeapMarshalBlocks{ts::round_up(hdr->unmarshal_size())};
  hdr = reinterpret_cast<HdrHeap *>(reinterpret_cast<char *>(alt) + reinterpret_cast<uintptr_t>(alt->m_response_hdr.m_heap));
  zret += HdrHeapMarshalBlocks{ts::round_up(hdr->unmarshal_size())};
  return zret;
}

// Copy an unmarshalled instance from @a src to @a dst.
// @a src is presumed to be Cache version 21 and the result
// is Cache version 23. @a length is the buffer available in @a dst.
// @return @c false if something went wrong (e.g., data overrun).
bool
HTTPInfo_v21::copy_and_upgrade_unmarshalled_to_v23(char *&dst, char *&src, size_t &length, int n_frags, FragOffset *frag_offsets)
{
  // Offsets of the data after the new stuff.
  static const size_t OLD_OFFSET = offsetof(HTTPCacheAlt_v21, m_ext_buffer);
  static const size_t NEW_OFFSET = offsetof(HTTPCacheAlt_v23, m_ext_buffer);

  HTTPCacheAlt_v21 *s_alt = reinterpret_cast<HTTPCacheAlt_v21 *>(src);
  HTTPCacheAlt_v23 *d_alt = reinterpret_cast<HTTPCacheAlt_v23 *>(dst);
  HdrHeap_v23 *s_hdr;
  HdrHeap_v23 *d_hdr;
  size_t hdr_size;

  if (length < HTTP_ALT_MARSHAL_SIZE) {
    return false; // Absolutely no hope in this case.
  }

  memcpy(dst, src, OLD_OFFSET); // initially same data
  // Now data that's now after extra
  memcpy(static_cast<char *>(dst) + NEW_OFFSET, static_cast<char *>(src) + OLD_OFFSET, sizeof(HTTPCacheAlt_v21) - OLD_OFFSET);
  dst += HTTP_ALT_MARSHAL_SIZE; // move past fixed data.
  length -= HTTP_ALT_MARSHAL_SIZE;

  // Extra data is fragment table - set that if we have it.
  if (n_frags) {
    static size_t const IFT_SIZE = HTTPCacheAlt_v23::N_INTEGRAL_FRAG_OFFSETS * sizeof(FragOffset);
    size_t ift_actual            = std::min(n_frags, HTTPCacheAlt_v23::N_INTEGRAL_FRAG_OFFSETS) * sizeof(FragOffset);

    if (length < (HTTP_ALT_MARSHAL_SIZE + n_frags * sizeof(FragOffset) - IFT_SIZE)) {
      return false; // can't place fragment table.
    }

    d_alt->m_frag_offset_count = n_frags;
    d_alt->m_frag_offsets      = reinterpret_cast<FragOffset *>(dst - reinterpret_cast<char *>(d_alt));

    memcpy(d_alt->m_integral_frag_offsets, frag_offsets, ift_actual);
    n_frags -= HTTPCacheAlt_v23::N_INTEGRAL_FRAG_OFFSETS;
    if (n_frags > 0) {
      size_t k = sizeof(FragOffset) * n_frags;
      memcpy(dst, frag_offsets + IFT_SIZE, k);
      dst += k;
      length -= k;
    } else if (n_frags < 0) {
      memset(dst + ift_actual, 0, IFT_SIZE - ift_actual);
    }
  } else {
    d_alt->m_frag_offset_count = 0;
    d_alt->m_frag_offsets      = nullptr;
    ink_zero(d_alt->m_integral_frag_offsets);
  }

  // Copy over the headers, tweaking the swizzled pointers.
  s_hdr =
    reinterpret_cast<HdrHeap_v23 *>(reinterpret_cast<char *>(s_alt) + reinterpret_cast<uintptr_t>(s_alt->m_request_hdr.m_heap));
  d_hdr    = reinterpret_cast<HdrHeap_v23 *>(dst);
  hdr_size = HdrHeapMarshalBlocks{ts::round_up(s_hdr->unmarshal_size())};
  if (hdr_size > length) {
    return false;
  }
  memcpy(static_cast<void *>(d_hdr), s_hdr, hdr_size);
  d_alt->m_request_hdr.m_heap = reinterpret_cast<HdrHeap_v23 *>(reinterpret_cast<char *>(d_hdr) - reinterpret_cast<char *>(d_alt));
  dst += hdr_size;
  length -= hdr_size;

  s_hdr =
    reinterpret_cast<HdrHeap_v23 *>(reinterpret_cast<char *>(s_alt) + reinterpret_cast<uintptr_t>(s_alt->m_response_hdr.m_heap));
  d_hdr    = reinterpret_cast<HdrHeap_v23 *>(dst);
  hdr_size = HdrHeapMarshalBlocks{ts::round_up(s_hdr->unmarshal_size())};
  if (hdr_size > length) {
    return false;
  }
  memcpy(static_cast<void *>(d_hdr), s_hdr, hdr_size);
  d_alt->m_response_hdr.m_heap = reinterpret_cast<HdrHeap_v23 *>(reinterpret_cast<char *>(d_hdr) - reinterpret_cast<char *>(d_alt));
  dst += hdr_size;
  length -= hdr_size;

  src = reinterpret_cast<char *>(s_hdr) + hdr_size;

  return true;
}

} // namespace cache_bc
