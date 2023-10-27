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

#include "iocore/cache/Cache.h"

// Cache Inspector and State Pages
#include "P_CacheTest.h"
#include "proxy/StatPages.h"

#include "tscore/Filenames.h"

#include "../../records/P_RecProcess.h"

#ifdef AIO_FAULT_INJECTION
#include "iocore/aio/AIO_fault_injection.h"
#endif

#include <atomic>

constexpr ts::VersionNumber CACHE_DB_VERSION(CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION);

static size_t DEFAULT_RAM_CACHE_MULTIPLIER = 10; // I.e. 10x 1MB per 1GB of disk.

// Configuration

int64_t cache_config_ram_cache_size            = AUTO_SIZE_RAM_CACHE;
int cache_config_ram_cache_algorithm           = 1;
int cache_config_ram_cache_compress            = 0;
int cache_config_ram_cache_compress_percent    = 90;
int cache_config_ram_cache_use_seen_filter     = 1;
int cache_config_http_max_alts                 = 3;
int cache_config_log_alternate_eviction        = 0;
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

// Globals

CacheStatsBlock cache_rsb;
Cache *theCache                         = nullptr;
CacheDisk **gdisks                      = nullptr;
int gndisks                             = 0;
static std::atomic<int> initialize_disk = 0;
Cache *caches[NUM_CACHE_FRAG_TYPES]     = {nullptr};
CacheSync *cacheDirSync                 = nullptr;
static Store theCacheStore;
int CacheProcessor::initialized          = CACHE_INITIALIZING;
uint32_t CacheProcessor::cache_ready     = 0;
int CacheProcessor::start_done           = 0;
bool CacheProcessor::clear               = false;
bool CacheProcessor::fix                 = false;
bool CacheProcessor::check               = false;
int CacheProcessor::start_internal_flags = 0;
int CacheProcessor::auto_clear_flag      = 0;
CacheProcessor cacheProcessor;
Stripe **gvol          = nullptr;
std::atomic<int> gnvol = 0;
ClassAllocator<CacheVC> cacheVConnectionAllocator("cacheVConnection");
ClassAllocator<CacheEvacuateDocVC> cacheEvacuateDocVConnectionAllocator("cacheEvacuateDocVC");
ClassAllocator<EvacuationBlock> evacuationBlockAllocator("evacuationBlock");
ClassAllocator<CacheRemoveCont> cacheRemoveContAllocator("cacheRemoveCont");
ClassAllocator<EvacuationKey> evacuationKeyAllocator("evacuationKey");

namespace
{

DbgCtl dbg_ctl_cache_init{"cache_init"};
DbgCtl dbg_ctl_cache_remove{"cache_remove"};
DbgCtl dbg_ctl_cache_hosting{"cache_hosting"};
DbgCtl dbg_ctl_ram_cache{"ram_cache"};

} // end anonymous namespace

void cplist_init();
static void cplist_update();
int cplist_reconfigure();
static int create_volume(int volume_number, off_t size_in_blocks, int scheme, CacheVol *cp);
static void rebuild_host_table(Cache *cache);

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

// Register Stats, this handles both the global cache metrics, as well as the per volume metrics.
static void
register_cache_stats(CacheStatsBlock *rsb, const std::string prefix)
{
  // These are special, in that we have 7 x 3 metrics here in a structure based on cache operation done
  rsb->status[static_cast<int>(CacheOpType::Lookup)].active    = Metrics::Counter::createPtr(prefix + ".lookup.active");
  rsb->status[static_cast<int>(CacheOpType::Lookup)].success   = Metrics::Counter::createPtr(prefix + ".lookup.success");
  rsb->status[static_cast<int>(CacheOpType::Lookup)].failure   = Metrics::Counter::createPtr(prefix + ".lookup.failure");
  rsb->status[static_cast<int>(CacheOpType::Read)].active      = Metrics::Counter::createPtr(prefix + ".read.active");
  rsb->status[static_cast<int>(CacheOpType::Read)].success     = Metrics::Counter::createPtr(prefix + ".read.success");
  rsb->status[static_cast<int>(CacheOpType::Read)].failure     = Metrics::Counter::createPtr(prefix + ".read.failure");
  rsb->status[static_cast<int>(CacheOpType::Write)].active     = Metrics::Counter::createPtr(prefix + ".write.active");
  rsb->status[static_cast<int>(CacheOpType::Write)].success    = Metrics::Counter::createPtr(prefix + ".write.success");
  rsb->status[static_cast<int>(CacheOpType::Write)].failure    = Metrics::Counter::createPtr(prefix + ".write.failure");
  rsb->status[static_cast<int>(CacheOpType::Update)].active    = Metrics::Counter::createPtr(prefix + ".update.active");
  rsb->status[static_cast<int>(CacheOpType::Update)].success   = Metrics::Counter::createPtr(prefix + ".update.success");
  rsb->status[static_cast<int>(CacheOpType::Update)].failure   = Metrics::Counter::createPtr(prefix + ".update.failure");
  rsb->status[static_cast<int>(CacheOpType::Remove)].active    = Metrics::Counter::createPtr(prefix + ".remove.active");
  rsb->status[static_cast<int>(CacheOpType::Remove)].success   = Metrics::Counter::createPtr(prefix + ".remove.success");
  rsb->status[static_cast<int>(CacheOpType::Remove)].failure   = Metrics::Counter::createPtr(prefix + ".remove.failure");
  rsb->status[static_cast<int>(CacheOpType::Evacuate)].active  = Metrics::Counter::createPtr(prefix + ".evacuate.active");
  rsb->status[static_cast<int>(CacheOpType::Evacuate)].success = Metrics::Counter::createPtr(prefix + ".evacuate.success");
  rsb->status[static_cast<int>(CacheOpType::Evacuate)].failure = Metrics::Counter::createPtr(prefix + ".evacuate.failure");
  rsb->status[static_cast<int>(CacheOpType::Scan)].active      = Metrics::Counter::createPtr(prefix + ".scan.active");
  rsb->status[static_cast<int>(CacheOpType::Scan)].success     = Metrics::Counter::createPtr(prefix + ".scan.success");
  rsb->status[static_cast<int>(CacheOpType::Scan)].failure     = Metrics::Counter::createPtr(prefix + ".scan.failure");

  // These are in an array of 1, 2 and 3+ fragment documents
  rsb->fragment_document_count[0] = Metrics::Counter::createPtr(prefix + ".frags_per_doc.1");
  rsb->fragment_document_count[1] = Metrics::Counter::createPtr(prefix + ".frags_per_doc.2");
  rsb->fragment_document_count[2] = Metrics::Counter::createPtr(prefix + ".frags_per_doc.3+");

  // And then everything else
  rsb->bytes_used            = Metrics::Gauge::createPtr(prefix + ".bytes_used");
  rsb->bytes_total           = Metrics::Gauge::createPtr(prefix + ".bytes_total");
  rsb->stripes               = Metrics::Gauge::createPtr(prefix + ".stripes");
  rsb->ram_cache_bytes_total = Metrics::Gauge::createPtr(prefix + ".ram_cache.total_bytes");
  rsb->ram_cache_bytes       = Metrics::Counter::createPtr(prefix + ".ram_cache.bytes_used");
  rsb->ram_cache_hits        = Metrics::Counter::createPtr(prefix + ".ram_cache.hits");
  rsb->ram_cache_misses      = Metrics::Counter::createPtr(prefix + ".ram_cache.misses");
  rsb->pread_count           = Metrics::Counter::createPtr(prefix + ".pread_count");
  rsb->percent_full          = Metrics::Gauge::createPtr(prefix + ".percent_full");
  rsb->read_seek_fail        = Metrics::Counter::createPtr(prefix + ".read.seek.failure");
  rsb->read_invalid          = Metrics::Counter::createPtr(prefix + ".read.invalid");
  rsb->write_backlog_failure = Metrics::Counter::createPtr(prefix + ".write.backlog.failure");
  rsb->direntries_total      = Metrics::Gauge::createPtr(prefix + ".direntries.total");
  rsb->direntries_used       = Metrics::Gauge::createPtr(prefix + ".direntries.used");
  rsb->directory_collision   = Metrics::Counter::createPtr(prefix + ".directory_collision");
  rsb->read_busy_success     = Metrics::Counter::createPtr(prefix + ".read_busy.success");
  rsb->read_busy_failure     = Metrics::Counter::createPtr(prefix + ".read_busy.failure");
  rsb->write_bytes           = Metrics::Counter::createPtr(prefix + ".write_bytes_stat");
  rsb->hdr_vector_marshal    = Metrics::Counter::createPtr(prefix + ".vector_marshals");
  rsb->hdr_marshal           = Metrics::Counter::createPtr(prefix + ".hdr_marshals");
  rsb->hdr_marshal_bytes     = Metrics::Counter::createPtr(prefix + ".hdr_marshal_bytes");
  rsb->gc_bytes_evacuated    = Metrics::Counter::createPtr(prefix + ".gc_bytes_evacuated");
  rsb->gc_frags_evacuated    = Metrics::Counter::createPtr(prefix + ".gc_frags_evacuated");
  rsb->directory_wrap        = Metrics::Counter::createPtr(prefix + ".wrap_count");
  rsb->directory_sync_count  = Metrics::Counter::createPtr(prefix + ".sync.count");
  rsb->directory_sync_bytes  = Metrics::Counter::createPtr(prefix + ".sync.bytes");
  rsb->directory_sync_time   = Metrics::Counter::createPtr(prefix + ".sync.time");
  rsb->span_errors_read      = Metrics::Counter::createPtr(prefix + ".span.errors.read");
  rsb->span_errors_write     = Metrics::Counter::createPtr(prefix + ".span.errors.write");
  rsb->span_failing          = Metrics::Gauge::createPtr(prefix + ".span.failing");
  rsb->span_offline          = Metrics::Gauge::createPtr(prefix + ".span.offline");
  rsb->span_online           = Metrics::Gauge::createPtr(prefix + ".span.online");
}

// ToDo: This gets called as part of librecords collection continuation, probably change this later.
inline int64_t
cache_bytes_used(int vol_ix)
{
  if (!DISK_BAD(gvol[vol_ix]->disk)) {
    if (!gvol[vol_ix]->header->cycle) {
      return gvol[vol_ix]->header->write_pos - gvol[vol_ix]->start;
    } else {
      return gvol[vol_ix]->len - gvol[vol_ix]->dirlen() - EVACUATION_SIZE;
    }
  }

  return 0;
}

void
CachePeriodicMetricsUpdate()
{
  int64_t total_sum = 0;

  // Make sure the bytes_used per volume is always reset to zero, this can update the
  // volume metric more than once (once per disk). This happens once every sync
  // period (5s), and nothing else modifies these metrics.
  for (int vol_ix = 0; vol_ix < gnvol; ++vol_ix) {
    Metrics::Gauge::store(gvol[vol_ix]->cache_vol->vol_rsb.bytes_used, 0);
  }

  if (cacheProcessor.initialized == CACHE_INITIALIZED) {
    for (int vol_ix = 0; vol_ix < gnvol; ++vol_ix) {
      Stripe *v    = gvol[vol_ix];
      int64_t used = cache_bytes_used(vol_ix);

      Metrics::Gauge::increment(v->cache_vol->vol_rsb.bytes_used, used); // This assumes they start at zero
      total_sum += used;
    }

    // Also update the global (not per volume) metrics
    int64_t total = Metrics::Gauge::load(cache_rsb.bytes_total);

    Metrics::Gauge::store(cache_rsb.bytes_used, total_sum);
    Metrics::Gauge::store(cache_rsb.percent_full, total ? (total_sum * 100) / total : 0);
  }
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

// Cache Processor

int
CacheProcessor::start(int, size_t)
{
  RecRegNewSyncStatSync(CachePeriodicMetricsUpdate);

  return start_internal(0);
}

static const int DEFAULT_CACHE_OPTIONS = (O_RDWR);

static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_READ) == static_cast<int>(CACHE_EVENT_OPEN_READ));
static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_READ_FAILED) == static_cast<int>(CACHE_EVENT_OPEN_READ_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_WRITE) == static_cast<int>(CACHE_EVENT_OPEN_WRITE));
static_assert(static_cast<int>(TS_EVENT_CACHE_OPEN_WRITE_FAILED) == static_cast<int>(CACHE_EVENT_OPEN_WRITE_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_REMOVE) == static_cast<int>(CACHE_EVENT_REMOVE));
static_assert(static_cast<int>(TS_EVENT_CACHE_REMOVE_FAILED) == static_cast<int>(CACHE_EVENT_REMOVE_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN) == static_cast<int>(CACHE_EVENT_SCAN));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_FAILED) == static_cast<int>(CACHE_EVENT_SCAN_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_OBJECT) == static_cast<int>(CACHE_EVENT_SCAN_OBJECT));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED) == static_cast<int>(CACHE_EVENT_SCAN_OPERATION_BLOCKED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_OPERATION_FAILED) == static_cast<int>(CACHE_EVENT_SCAN_OPERATION_FAILED));
static_assert(static_cast<int>(TS_EVENT_CACHE_SCAN_DONE) == static_cast<int>(CACHE_EVENT_SCAN_DONE));

int
CacheProcessor::start_internal(int flags)
{
  start_internal_flags = flags;
  clear                = !!(flags & PROCESSOR_RECONFIGURE) || auto_clear_flag;
  fix                  = !!(flags & PROCESSOR_FIX);
  check                = (flags & PROCESSOR_CHECK) != 0;
  start_done           = 0;

  /* Read the config file and create the data structures corresponding to the file. */
  gndisks = theCacheStore.n_disks;
  gdisks  = static_cast<CacheDisk **>(ats_malloc(gndisks * sizeof(CacheDisk *)));

  // Temporaries to carry values between loops
  char **paths = static_cast<char **>(alloca(sizeof(char *) * gndisks));
  memset(paths, 0, sizeof(char *) * gndisks);
  int *fds = static_cast<int *>(alloca(sizeof(int) * gndisks));
  memset(fds, 0, sizeof(int) * gndisks);
  int *sector_sizes = static_cast<int *>(alloca(sizeof(int) * gndisks));
  memset(sector_sizes, 0, sizeof(int) * gndisks);
  Span **sds = static_cast<Span **>(alloca(sizeof(Span *) * gndisks));
  memset(sds, 0, sizeof(Span *) * gndisks);

  gndisks = 0;
  ink_aio_set_err_callback(new AIO_failure_handler());

  config_volumes.read_config_file();

  /*
   create CacheDisk objects for each span in the configuration file and store in gdisks
   */
  for (unsigned i = 0; i < theCacheStore.n_disks; i++) {
    Span *sd = theCacheStore.disk[i];
    int opts = DEFAULT_CACHE_OPTIONS;

    if (!paths[gndisks]) {
      paths[gndisks] = static_cast<char *>(alloca(PATH_NAME_MAX));
    }
    ink_strlcpy(paths[gndisks], sd->pathname, PATH_NAME_MAX);
    if (!sd->file_pathname) {
      ink_strlcat(paths[gndisks], "/cache.db", PATH_NAME_MAX);
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

#ifdef AIO_FAULT_INJECTION
    int fd = aioFaultInjection.open(paths[gndisks], opts, 0644);
#else
    int fd = open(paths[gndisks], opts, 0644);
#endif
    int64_t blocks = sd->blocks;

    if (fd < 0 && (opts & O_CREAT)) { // Try without O_DIRECT if this is a file on filesystem, e.g. tmpfs.
#ifdef AIO_FAULT_INJECTION
      fd = aioFaultInjection.open(paths[gndisks], DEFAULT_CACHE_OPTIONS | O_CREAT, 0644);
#else
      fd = open(paths[gndisks], DEFAULT_CACHE_OPTIONS | O_CREAT, 0644);
#endif
    }

    if (fd >= 0) {
      bool diskok = true;
      if (!sd->file_pathname) {
        if (!check) {
          if (ftruncate(fd, blocks * STORE_BLOCK_SIZE) < 0) {
            Warning("unable to truncate cache file '%s' to %" PRId64 " blocks", paths[gndisks], blocks);
            diskok = false;
          }
        } else { // read-only mode checks
          struct stat sbuf;
          if (-1 == fstat(fd, &sbuf)) {
            fprintf(stderr, "Failed to stat cache file for directory %s\n", paths[gndisks]);
            diskok = false;
          } else if (blocks != sbuf.st_size / STORE_BLOCK_SIZE) {
            fprintf(stderr, "Cache file for directory %s is %" PRId64 " bytes, expected %" PRId64 "\n", paths[gndisks],
                    sbuf.st_size, blocks * static_cast<int64_t>(STORE_BLOCK_SIZE));
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
        // storage systems increasingly want larger I/Os. For example, on macOS, the filesystem
        // block size is always reported as 1MB.
        if (sd->hw_sector_size <= 0 || sector_size > STORE_BLOCK_SIZE) {
          Note("resetting hardware sector size from %d to %d", sector_size, STORE_BLOCK_SIZE);
          sector_size = STORE_BLOCK_SIZE;
        }
        sector_sizes[gndisks] = sector_size;
        fds[gndisks]          = fd;
        sds[gndisks]          = sd;
        fd                    = -1;
        gndisks++;
      }
    } else {
      if (errno == EINVAL) {
        Warning("cache unable to open '%s': It must be placed on a file system that supports direct I/O.", paths[gndisks]);
      } else {
        Warning("cache unable to open '%s': %s", paths[gndisks], strerror(errno));
      }
    }
    if (fd >= 0) {
      close(fd);
    }
  }

  // Before we kick off asynchronous operations, make sure sufficient disks are available and we don't just shutdown
  // Exiting with background threads in operation will likely cause a seg fault
  start_done = 1;

  if (gndisks == 0) {
    CacheProcessor::initialized = CACHE_INIT_FAILED;
    // Have to do this here because no IO events were scheduled and so @c diskInitialized() won't be called.
    if (cb_after_init) {
      cb_after_init();
    }

    if (this->waitForCache() > 1) {
      Emergency("Cache initialization failed - no disks available but cache required");
    } else {
      Warning("unable to open cache disk(s): Cache Disabled\n");
      return -1; // pointless, AFAICT this is ignored.
    }
  } else if (this->waitForCache() == 3 && static_cast<unsigned int>(gndisks) < theCacheStore.n_disks_in_config) {
    CacheProcessor::initialized = CACHE_INIT_FAILED;
    if (cb_after_init) {
      cb_after_init();
    }
    Emergency("Cache initialization failed - only %d out of %d disks were valid and all were required.", gndisks,
              theCacheStore.n_disks_in_config);
  } else if (this->waitForCache() == 2 && static_cast<unsigned int>(gndisks) < theCacheStore.n_disks_in_config) {
    Warning("Cache initialization incomplete - only %d out of %d disks were valid.", gndisks, theCacheStore.n_disks_in_config);
  }

  // If we got here, we have enough disks to proceed
  for (int j = 0; j < gndisks; j++) {
    Span *sd = sds[j];
    ink_release_assert(sds[j] != nullptr); // Defeat clang-analyzer
    off_t skip     = ROUND_TO_STORE_BLOCK((sd->offset < START_POS ? START_POS + sd->alignment : sd->offset));
    int64_t blocks = sd->blocks - (skip >> STORE_BLOCK_SHIFT);
    gdisks[j]->open(paths[j], blocks, skip, sector_sizes[j], fds[j], clear);

    Dbg(dbg_ctl_cache_hosting, "Disk: %d:%s, blocks: %" PRId64 "", gndisks, paths[j], blocks);
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
      // This could be passed off to @c cacheInitialized (as with volume config problems) but I
      // think the more specific error message here is worth the extra code.
      CacheProcessor::initialized = CACHE_INIT_FAILED;
      if (cb_after_init) {
        cb_after_init();
      }
      Emergency("Cache initialization failed - only %d of %d disks were available.", gndisks, theCacheStore.n_disks_in_config);
    } else if (this->waitForCache() == 2) {
      Warning("Cache initialization incomplete - only %d of %d disks were available.", gndisks, theCacheStore.n_disks_in_config);
    }
  }

  /* Practically just took all bad_disks offline so update the stats. */
  // ToDo: These don't get update on the per-volume metrics :-/
  Metrics::Gauge::store(cache_rsb.span_offline, bad_disks);
  Metrics::Gauge::decrement(cache_rsb.span_failing, bad_disks);
  Metrics::Gauge::store(cache_rsb.span_online, gndisks);

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
      char vol_stat_str_prefix[256];

      snprintf(vol_stat_str_prefix, sizeof(vol_stat_str_prefix), "proxy.process.cache.volume_%d", cp->vol_number);
      register_cache_stats(&cp->vol_rsb, vol_stat_str_prefix);
    }
  }

  gvol = static_cast<Stripe **>(ats_malloc(gnvol * sizeof(Stripe *)));
  memset(gvol, 0, gnvol * sizeof(Stripe *));
  gnvol = 0;
  for (i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    if (dbg_ctl_cache_hosting.on()) {
      int j;
      DbgPrint(dbg_ctl_cache_hosting, "Disk: %d:%s: Stripe Blocks: %u: Free space: %" PRIu64, i, d->path,
               d->header->num_diskvol_blks, d->free_space);
      for (j = 0; j < static_cast<int>(d->header->num_volumes); j++) {
        DbgPrint(dbg_ctl_cache_hosting, "\tStripe: %d Size: %" PRIu64, d->disk_vols[j]->vol_number, d->disk_vols[j]->size);
      }
      for (j = 0; j < static_cast<int>(d->header->num_diskvol_blks); j++) {
        DbgPrint(dbg_ctl_cache_hosting, "\tBlock No: %d Size: %" PRIu64 " Free: %u", d->header->vol_info[j].number,
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
     volume occupies */
  int64_t total_size             = 0; // count in HTTP & MIXT
  uint64_t total_cache_bytes     = 0; // bytes that can used in total_size
  uint64_t total_direntries      = 0; // all the direntries in the cache
  uint64_t used_direntries       = 0; //   and used
  uint64_t vol_total_cache_bytes = 0;
  uint64_t vol_total_direntries  = 0;
  uint64_t vol_used_direntries   = 0;
  Stripe *vol;

  if (theCache) {
    total_size += theCache->cache_size;
    Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - theCache, total_size = %" PRId64 " = %" PRId64 " MB", total_size,
        total_size / ((1024 * 1024) / STORE_BLOCK_SIZE));
    if (theCache->ready == CACHE_INIT_FAILED) {
      Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - failed to initialize the cache "
                              "for http: cache disabled");
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
    Stripe *v = gvol[i];
    if (v->header->version < cacheProcessor.min_stripe_version) {
      cacheProcessor.min_stripe_version = v->header->version;
    }
    if (cacheProcessor.max_stripe_version < v->header->version) {
      cacheProcessor.max_stripe_version = v->header->version;
    }
  }

  if (caches_ready) {
    Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - caches_ready=0x%0X, gnvol=%d", (unsigned int)caches_ready,
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
        Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - cache_config_ram_cache_size == AUTO_SIZE_RAM_CACHE");
        for (i = 0; i < gnvol; i++) {
          vol = gvol[i];

          if (gvol[i]->cache_vol->ramcache_enabled) {
            gvol[i]->ram_cache->init(vol->dirlen() * DEFAULT_RAM_CACHE_MULTIPLIER, vol);
            ram_cache_bytes += gvol[i]->dirlen();
            Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - ram_cache_bytes = %" PRId64 " = %" PRId64 "Mb",
                ram_cache_bytes, ram_cache_bytes / (1024 * 1024));
            Metrics::Gauge::increment(vol->cache_vol->vol_rsb.ram_cache_bytes_total, gvol[i]->dirlen());
          }
          vol_total_cache_bytes  = gvol[i]->len - gvol[i]->dirlen();
          total_cache_bytes     += vol_total_cache_bytes;
          Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - total_cache_bytes = %" PRId64 " = %" PRId64 "Mb",
              total_cache_bytes, total_cache_bytes / (1024 * 1024));

          Metrics::Gauge::increment(vol->cache_vol->vol_rsb.bytes_total, vol_total_cache_bytes);

          vol_total_direntries  = gvol[i]->buckets * gvol[i]->segments * DIR_DEPTH;
          total_direntries     += vol_total_direntries;
          Metrics::Gauge::increment(vol->cache_vol->vol_rsb.direntries_total, vol_total_direntries);

          vol_used_direntries = dir_entries_used(gvol[i]);
          Metrics::Gauge::increment(vol->cache_vol->vol_rsb.direntries_used, vol_used_direntries);
          used_direntries += vol_used_direntries;
        }

      } else {
        // we got configured memory size
        // TODO, should we check the available system memories, or you will
        //   OOM or swapout, that is not a good situation for the server
        Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - %" PRId64 " != AUTO_SIZE_RAM_CACHE",
            cache_config_ram_cache_size);
        int64_t http_ram_cache_size =
          (theCache) ?
            static_cast<int64_t>((static_cast<double>(theCache->cache_size) / total_size) * cache_config_ram_cache_size) :
            0;
        Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - http_ram_cache_size = %" PRId64 " = %" PRId64 "Mb",
            http_ram_cache_size, http_ram_cache_size / (1024 * 1024));
        int64_t stream_ram_cache_size = cache_config_ram_cache_size - http_ram_cache_size;
        Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - stream_ram_cache_size = %" PRId64 " = %" PRId64 "Mb",
            stream_ram_cache_size, stream_ram_cache_size / (1024 * 1024));

        // Dump some ram_cache size information in debug mode.
        Dbg(dbg_ctl_ram_cache, "config: size = %" PRId64 ", cutoff = %" PRId64 "", cache_config_ram_cache_size,
            cache_config_ram_cache_cutoff);

        for (i = 0; i < gnvol; i++) {
          vol = gvol[i];
          double factor;
          if (gvol[i]->cache == theCache && gvol[i]->cache_vol->ramcache_enabled) {
            ink_assert(gvol[i]->cache != nullptr);
            factor = static_cast<double>(static_cast<int64_t>(gvol[i]->len >> STORE_BLOCK_SHIFT)) / theCache->cache_size;
            Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - factor = %f", factor);
            gvol[i]->ram_cache->init(static_cast<int64_t>(http_ram_cache_size * factor), vol);
            ram_cache_bytes += static_cast<int64_t>(http_ram_cache_size * factor);
            Metrics::Gauge::increment(vol->cache_vol->vol_rsb.ram_cache_bytes_total,
                                      static_cast<int64_t>(http_ram_cache_size * factor));
          } else if (gvol[i]->cache_vol->ramcache_enabled) {
            ink_release_assert(!"Unexpected non-HTTP cache volume");
          }
          Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized[%d] - ram_cache_bytes = %" PRId64 " = %" PRId64 "Mb", i,
              ram_cache_bytes, ram_cache_bytes / (1024 * 1024));
          vol_total_cache_bytes  = gvol[i]->len - gvol[i]->dirlen();
          total_cache_bytes     += vol_total_cache_bytes;
          Metrics::Gauge::increment(vol->cache_vol->vol_rsb.bytes_total, vol_total_cache_bytes);
          Metrics::Gauge::increment(vol->cache_vol->vol_rsb.stripes);
          Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - total_cache_bytes = %" PRId64 " = %" PRId64 "Mb",
              total_cache_bytes, total_cache_bytes / (1024 * 1024));

          vol_total_direntries  = gvol[i]->buckets * gvol[i]->segments * DIR_DEPTH;
          total_direntries     += vol_total_direntries;
          Metrics::Gauge::increment(vol->cache_vol->vol_rsb.direntries_total, vol_total_direntries);

          vol_used_direntries = dir_entries_used(gvol[i]);
          Metrics::Gauge::increment(vol->cache_vol->vol_rsb.direntries_used, vol_used_direntries);
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
        break;
      case CACHE_COMPRESSION_LIBLZMA:
#ifndef HAVE_LZMA_H
        Fatal("lzma not available for RAM cache compression");
#endif
        break;
      }

      Metrics::Gauge::store(cache_rsb.ram_cache_bytes_total, ram_cache_bytes);
      Metrics::Gauge::store(cache_rsb.bytes_total, total_cache_bytes);
      Metrics::Gauge::store(cache_rsb.direntries_total, total_direntries);
      Metrics::Gauge::store(cache_rsb.direntries_used, used_direntries);

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
    Emergency("Cache initialization failed with cache required, exiting.");
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

Action *
CacheProcessor::lookup(Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname, int host_len)
{
  return caches[frag_type]->lookup(cont, key, frag_type, hostname, host_len);
}

Action *
CacheProcessor::open_read(Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname, int hostlen)
{
  return caches[frag_type]->open_read(cont, key, frag_type, hostname, hostlen);
}

Action *
CacheProcessor::open_write(Continuation *cont, CacheKey *key, CacheFragType frag_type, int expected_size ATS_UNUSED, int options,
                           time_t pin_in_cache, char *hostname, int host_len)
{
  return caches[frag_type]->open_write(cont, key, frag_type, options, pin_in_cache, hostname, host_len);
}

Action *
CacheProcessor::remove(Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname, int host_len)
{
  Dbg(dbg_ctl_cache_remove, "[CacheProcessor::remove] Issuing cache delete for %u", cache_hash(*key));
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
  return static_cast<bool>(cache_ready & (1 << type));
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
  unsigned int *mapping = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  Stripe **p            = static_cast<Stripe **>(ats_malloc(sizeof(Stripe *) * num_vols));

  memset(mapping, 0, num_vols * sizeof(unsigned int));
  memset(p, 0, num_vols * sizeof(Stripe *));
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
    mapping[map]  = i;
    p[map++]      = cp->vols[i];
    total        += (cp->vols[i]->len >> STORE_BLOCK_SHIFT);
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

  unsigned int *forvol   = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned int *gotvol   = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned int *rnd      = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned short *ttable = static_cast<unsigned short *>(ats_malloc(sizeof(unsigned short) * VOL_HASH_TABLE_SIZE));
  unsigned short *old_table;
  unsigned int *rtable_entries = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned int rtable_size     = 0;

  // estimate allocation
  for (int i = 0; i < num_vols; i++) {
    forvol[i]          = (VOL_HASH_TABLE_SIZE * (p[i]->len >> STORE_BLOCK_SHIFT)) / total;
    used              += forvol[i];
    rtable_entries[i]  = p[i]->len / VOL_HASH_ALLOC_SIZE;
    rtable_size       += rtable_entries[i];
    gotvol[i]          = 0;
  }
  // spread around the excess
  int extra = VOL_HASH_TABLE_SIZE - used;
  for (int i = 0; i < extra; i++) {
    forvol[i % num_vols]++;
  }
  // seed random number generator
  for (int i = 0; i < num_vols; i++) {
    uint64_t x = p[i]->hash_id.fold();
    rnd[i]     = static_cast<unsigned int>(x);
  }
  // initialize table to "empty"
  for (int i = 0; i < VOL_HASH_TABLE_SIZE; i++) {
    ttable[i] = VOL_HASH_EMPTY;
  }
  // generate random numbers proportional to allocation
  rtable_pair *rtable = static_cast<rtable_pair *>(ats_malloc(sizeof(rtable_pair) * rtable_size));
  int rindex          = 0;
  for (int i = 0; i < num_vols; i++) {
    for (int j = 0; j < static_cast<int>(rtable_entries[i]); j++) {
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
    while (pos > rtable[i].rval && i < static_cast<int>(rtable_size) - 1) {
      i++;
    }
    ttable[j] = mapping[rtable[i].idx];
    gotvol[rtable[i].idx]++;
  }
  for (int i = 0; i < num_vols; i++) {
    Dbg(dbg_ctl_cache_init, "build_vol_hash_table index %d mapped to %d requested %d got %d", i, mapping[i], forvol[i], gotvol[i]);
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
      total_dir_delete   += gvol[p]->buckets * gvol[p]->segments * DIR_DEPTH;
      used_dir_delete    += dir_entries_used(gvol[p]);
      total_bytes_delete += gvol[p]->len - gvol[p]->dirlen();
    }
  }

  Metrics::Gauge::decrement(cache_rsb.bytes_total, total_bytes_delete);
  Metrics::Gauge::decrement(cache_rsb.direntries_total, total_dir_delete);
  Metrics::Gauge::decrement(cache_rsb.direntries_used, used_dir_delete);

  /* Update the span metrics, if failing then move the span from "failing" to "offline" bucket
   * if operator took it offline, move it from "online" to "offline" bucket */
  Metrics::Gauge::decrement(admin ? cache_rsb.span_online : cache_rsb.span_failing);
  Metrics::Gauge::increment(cache_rsb.span_offline);

  if (theCache) {
    rebuild_host_table(theCache);
  }

  zret = this->has_online_storage();
  if (!zret) {
    Warning("All storage devices offline, cache disabled");
    CacheProcessor::cache_ready = 0;
  } else { // check cache types specifically
    if (theCache) {
      ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(&theCache->hosttable);
      if (!hosttable->gen_host_rec.vol_hash_table) {
        unsigned int caches_ready    = 0;
        caches_ready                 = caches_ready | (1 << CACHE_FRAG_TYPE_HTTP);
        caches_ready                 = caches_ready | (1 << CACHE_FRAG_TYPE_NONE);
        caches_ready                 = ~caches_ready;
        CacheProcessor::cache_ready &= caches_ready;
        Warning("all volumes for http cache are corrupt, http cache disabled");
      }
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
AIO_failure_handler::handle_disk_failure(int /* event ATS_UNUSED */, void *data)
{
  /* search for the matching file descriptor */
  if (!CacheProcessor::cache_ready) {
    return EVENT_DONE;
  }
  int disk_no     = 0;
  AIOCallback *cb = static_cast<AIOCallback *>(data);

  for (; disk_no < gndisks; disk_no++) {
    CacheDisk *d = gdisks[disk_no];

    if (d->fd == cb->aiocb.aio_fildes) {
      char message[256];
      d->incrErrors(cb);

      if (!DISK_BAD(d)) {
        snprintf(message, sizeof(message), "Error accessing Disk %s [%d/%d]", d->path, d->num_errors, cache_config_max_disk_errors);
        Warning("%s", message);
      } else if (!DISK_BAD_SIGNALLED(d)) {
        snprintf(message, sizeof(message), "too many errors accessing disk %s [%d/%d]: declaring disk bad", d->path, d->num_errors,
                 cache_config_max_disk_errors);
        Warning("%s", message);
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

  {
    CacheHostTable *hosttable_raw = new CacheHostTable(this, scheme);
    hosttable.reset(hosttable_raw);
    hosttable_raw->register_config_callback(&hosttable);
  }

  ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(&this->hosttable);
  if (hosttable->gen_host_rec.num_cachevols == 0) {
    ready = CACHE_INIT_FAILED;
  } else {
    ready = CACHE_INITIALIZED;
  }

  // TS-3848
  if (ready == CACHE_INIT_FAILED && cacheProcessor.waitForCache() >= 2) {
    Emergency("Failed to initialize cache host table");
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
  Dbg(dbg_ctl_cache_init, "Cache::open - proxy.config.cache.min_average_object_size = %d",
      (int)cache_config_min_average_object_size);

  CacheVol *cp = cp_list.head;
  for (; cp; cp = cp->link.next) {
    if (cp->scheme == scheme) {
      cp->vols   = static_cast<Stripe **>(ats_malloc(cp->num_vols * sizeof(Stripe *)));
      int vol_no = 0;
      for (i = 0; i < gndisks; i++) {
        if (cp->disk_vols[i] && !DISK_BAD(cp->disk_vols[i]->disk)) {
          DiskStripeBlockQueue *q = cp->disk_vols[i]->dpb_queue.head;
          for (; q; q = q->link.next) {
            cp->vols[vol_no]            = new Stripe();
            CacheDisk *d                = cp->disk_vols[i]->disk;
            cp->vols[vol_no]->disk      = d;
            cp->vols[vol_no]->fd        = d->fd;
            cp->vols[vol_no]->cache     = this;
            cp->vols[vol_no]->cache_vol = cp;
            blocks                      = q->b->len;

            bool vol_clear = clear || d->cleared || q->new_block;
            cp->vols[vol_no]->init(d->path, blocks, q->b->offset, vol_clear);
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

Action *
Cache::lookup(Continuation *cont, const CacheKey *key, CacheFragType type, const char *hostname, int host_len)
{
  if (!CacheProcessor::IsCacheReady(type)) {
    cont->handleEvent(CACHE_EVENT_LOOKUP_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  Stripe *vol = key_to_vol(key, hostname, host_len);
  CacheVC *c  = new_CacheVC(cont);
  SET_CONTINUATION_HANDLER(c, &CacheVC::openReadStartHead);
  c->vio.op  = VIO::READ;
  c->op_type = static_cast<int>(CacheOpType::Lookup);
  Metrics::Counter::increment(cache_rsb.status[c->op_type].active);
  Metrics::Counter::increment(vol->cache_vol->vol_rsb.status[c->op_type].active);
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
  Stripe *vol = key_to_vol(key, hostname, host_len);
  // coverity[var_decl]
  Dir result;
  dir_clear(&result); // initialized here, set result empty so we can recognize missed lock
  mutex = cont->mutex;

  CacheVC *c   = new_CacheVC(cont);
  c->vio.op    = VIO::NONE;
  c->frag_type = type;
  c->op_type   = static_cast<int>(CacheOpType::Remove);
  Metrics::Counter::increment(cache_rsb.status[c->op_type].active);
  Metrics::Counter::increment(vol->cache_vol->vol_rsb.status[c->op_type].active);
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
    ink_assert(d != nullptr);
    DiskStripe **dp = d->disk_vols;
    for (unsigned int j = 0; j < d->header->num_volumes; j++) {
      ink_assert(dp[j]->dpb_queue.head);
      CacheVol *p = cp_list.head;
      while (p) {
        if (p->vol_number == dp[j]->vol_number) {
          ink_assert(p->scheme == (int)dp[j]->dpb_queue.head->b->type);
          p->size         += dp[j]->size;
          p->num_vols     += dp[j]->num_volblocks;
          p->disk_vols[i]  = dp[j];
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
        new_p->disk_vols  = static_cast<DiskStripe **>(ats_malloc(gndisks * sizeof(DiskStripe *)));
        memset(new_p->disk_vols, 0, gndisks * sizeof(DiskStripe *));
        new_p->disk_vols[i] = dp[j];
        cp_list.enqueue(new_p);
        cp_list_len++;
      }
    }
  }
}

static int fillExclusiveDisks(CacheVol *cp);

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
          cp->ramcache_enabled = config_vol->ramcache_enabled;
          config_vol->cachep   = cp;
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
      // Delete the volume from the cache vol list
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

  // Look for (exclusive) spans forced to a specific volume but not yet referenced by any volumes in cp_list,
  // if found then create a new volume. This also makes sure new exclusive disk volumes are created first
  // before any other new volumes to assure proper span free space calculation and proper volume block distribution.
  for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
    if (nullptr == config_vol->cachep) {
      // Find out if this is a forced volume assigned exclusively to a span which was cleared (hence
      // not referenced in cp_list). Note: non-exclusive cleared spans are not handled here, only
      // the "exclusive"
      bool forced_volume = false;
      for (int d_no = 0; d_no < gndisks; d_no++) {
        if (gdisks[d_no]->forced_volume_num == config_vol->number) {
          forced_volume = true;
        }
      }

      if (forced_volume) {
        CacheVol *new_cp = new CacheVol();
        if (nullptr != new_cp) {
          new_cp->disk_vols = static_cast<decltype(new_cp->disk_vols)>(ats_malloc(gndisks * sizeof(DiskStripe *)));
          if (nullptr != new_cp->disk_vols) {
            memset(new_cp->disk_vols, 0, gndisks * sizeof(DiskStripe *));
            new_cp->vol_number = config_vol->number;
            new_cp->scheme     = config_vol->scheme;
            config_vol->cachep = new_cp;
            fillExclusiveDisks(config_vol->cachep);
            cp_list.enqueue(new_cp);
          } else {
            delete new_cp;
          }
        }
      }
    } else {
      // Fill if this is exclusive disk.
      fillExclusiveDisks(config_vol->cachep);
    }
  }
}

static int
fillExclusiveDisks(CacheVol *cp)
{
  int diskCount     = 0;
  int volume_number = cp->vol_number;

  Dbg(dbg_ctl_cache_init, "volume %d", volume_number);
  for (int i = 0; i < gndisks; i++) {
    if (gdisks[i]->forced_volume_num != volume_number) {
      continue;
    }

    /* OK, this should be an "exclusive" disk (span). */
    diskCount++;

    /* There should be a single "forced" volume and no other volumes should exist on this "exclusive" disk (span) */
    bool found_nonforced_volumes = false;
    for (int j = 0; j < static_cast<int>(gdisks[i]->header->num_volumes); j++) {
      if (volume_number != gdisks[i]->disk_vols[j]->vol_number) {
        found_nonforced_volumes = true;
        break;
      }
    }

    if (found_nonforced_volumes) {
      /* The user had created several volumes before - clear the disk and create one volume for http
       */
      Note("Clearing Disk: %s", gdisks[i]->path);
      gdisks[i]->delete_all_volumes();
    } else if (1 == gdisks[i]->header->num_volumes) {
      /* "Forced" volumes take the whole disk (span) hence nothing more to do for this span. */
      continue;
    }

    /* Now, volumes have been either deleted or did not exist to begin with so we need to create them. */

    int64_t size_diff = gdisks[i]->num_usable_blocks;
    DiskStripeBlock *dpb;
    do {
      dpb = gdisks[i]->create_volume(volume_number, size_diff, cp->scheme);
      if (dpb) {
        if (!cp->disk_vols[i]) {
          cp->disk_vols[i] = gdisks[i]->get_diskvol(volume_number);
        }
        size_diff -= dpb->len;
        cp->size  += dpb->len;
        cp->num_vols++;
      } else {
        Dbg(dbg_ctl_cache_init, "create_volume failed");
        break;
      }
    } while ((size_diff > 0));
  }

  /* Report back the number of disks (spans) that were assigned to volume specified by volume_number. */
  return diskCount;
}

int
cplist_reconfigure()
{
  int64_t size;
  int volume_number;
  off_t size_in_blocks;
  ConfigVol *config_vol;
  int assignedVol = 0; // Number of assigned volumes

  gnvol = 0;
  if (config_volumes.num_volumes == 0) {
    /* only the http cache */
    CacheVol *cp   = new CacheVol();
    cp->vol_number = 0;
    cp->scheme     = CACHE_HTTP_TYPE;
    cp->disk_vols  = static_cast<DiskStripe **>(ats_malloc(gndisks * sizeof(DiskStripe *)));
    memset(cp->disk_vols, 0, gndisks * sizeof(DiskStripe *));
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
          Dbg(dbg_ctl_cache_hosting, "blocks = %" PRId64, (int64_t)b);
          DiskStripeBlock *dpb = gdisks[i]->create_volume(0, b, CACHE_HTTP_TYPE);
          ink_assert(dpb && dpb->len == (uint64_t)b);
        }
        ink_assert(gdisks[i]->free_space == 0);
      }

      ink_assert(gdisks[i]->header->num_volumes == 1);
      DiskStripe **dp   = gdisks[i]->disk_vols;
      gnvol            += dp[0]->num_volblocks;
      cp->size         += dp[0]->size;
      cp->num_vols     += dp[0]->num_volblocks;
      cp->disk_vols[i]  = dp[0];
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

    /* change percentages in the config partitions to absolute value */
    off_t tot_space_in_blks = 0;
    off_t blocks_per_vol    = VOL_BLOCK_SIZE / STORE_BLOCK_SIZE;
    /* sum up the total space available on all the disks.
       round down the space to 128 megabytes */
    for (int i = 0; i < gndisks; i++) {
      // Exclude exclusive disks (with forced volumes) from the following total space calculation,
      // in such a way forced volumes will not impact volume percentage calculations.
      if (-1 == gdisks[i]->forced_volume_num) {
        tot_space_in_blks += (gdisks[i]->num_usable_blocks / blocks_per_vol) * blocks_per_vol;
      } else {
        ++assignedVol;
      }
    }

    double percent_remaining = 100.00;
    for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
      if (config_vol->in_percent) {
        if (config_vol->percent > percent_remaining) {
          Warning("total volume sizes added up to more than 100%%!");
          Warning("no volumes created");
          return -1;
        }

        // Find if the volume is forced and if it is then calculate the total forced volume size.
        // Forced volumes take the whole span (disk) also sum all disk space this volume is forced
        // to.
        int64_t tot_forced_space_in_blks = 0;
        for (int i = 0; i < gndisks; i++) {
          if (config_vol->number == gdisks[i]->forced_volume_num) {
            tot_forced_space_in_blks += (gdisks[i]->num_usable_blocks / blocks_per_vol) * blocks_per_vol;
          }
        }

        int64_t space_in_blks = 0;
        if (0 == tot_forced_space_in_blks) {
          // Calculate the space as percentage of total space in blocks.
          space_in_blks = static_cast<int64_t>(((config_vol->percent / percent_remaining)) * tot_space_in_blks);
        } else {
          // Forced volumes take all disk space, so no percentage calculations here.
          space_in_blks = tot_forced_space_in_blks;
        }

        space_in_blks = space_in_blks >> (20 - STORE_BLOCK_SHIFT);
        /* round down to 128 megabyte multiple */
        space_in_blks    = (space_in_blks >> 7) << 7;
        config_vol->size = space_in_blks;

        if (0 == tot_forced_space_in_blks) {
          tot_space_in_blks -= space_in_blks << (20 - STORE_BLOCK_SHIFT);
          percent_remaining -= (config_vol->size < 128) ? 0 : config_vol->percent;
        }
      }
      if (config_vol->size < 128) {
        Warning("the size of volume %d (%" PRId64 ") is less than the minimum required volume size %d", config_vol->number,
                (int64_t)config_vol->size, 128);
        Warning("volume %d is not created", config_vol->number);
      }
      Dbg(dbg_ctl_cache_hosting, "Volume: %d Size: %" PRId64 " Ramcache: %d", config_vol->number, (int64_t)config_vol->size,
          config_vol->ramcache_enabled);
    }
    cplist_update();

    /* go through volume config and grow and create volumes */
    for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
      size = config_vol->size;
      if (size < 128) {
        continue;
      }

      volume_number = config_vol->number;

      size_in_blocks = (static_cast<off_t>(size) * 1024 * 1024) / STORE_BLOCK_SIZE;

      if (config_vol->cachep && config_vol->cachep->num_vols > 0) {
        gnvol += config_vol->cachep->num_vols;
        continue;
      }

      if (!config_vol->cachep) {
        // we did not find a corresponding entry in cache vol...create one

        CacheVol *new_cp  = new CacheVol();
        new_cp->disk_vols = static_cast<DiskStripe **>(ats_malloc(gndisks * sizeof(DiskStripe *)));
        memset(new_cp->disk_vols, 0, gndisks * sizeof(DiskStripe *));
        if (create_volume(config_vol->number, size_in_blocks, config_vol->scheme, new_cp)) {
          ats_free(new_cp->disk_vols);
          new_cp->disk_vols = nullptr;
          delete new_cp;
          return -1;
        }
        cp_list.enqueue(new_cp);
        cp_list_len++;
        config_vol->cachep  = new_cp;
        gnvol              += new_cp->num_vols;
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
          int curr         = sorted_vols[j];
          DiskStripe *dvol = cp->disk_vols[curr];
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
        /* if size_diff == 0, then the disks have volumes of the
           same sizes, so we don't need to balance the disks */
        if (size_diff == 0) {
          break;
        }

        DiskStripeBlock *dpb;
        do {
          dpb = gdisks[disk_no]->create_volume(volume_number, size_diff, cp->scheme);
          if (dpb) {
            if (!cp->disk_vols[disk_no]) {
              cp->disk_vols[disk_no] = gdisks[disk_no]->get_diskvol(volume_number);
            }
            size_diff -= dpb->len;
            cp->size  += dpb->len;
            cp->num_vols++;
          } else {
            break;
          }
        } while ((size_diff > 0));

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

  Metrics::Gauge::store(cache_rsb.stripes, gnvol + assignedVol);

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
    Dbg(dbg_ctl_cache_init, "volume successfully filled from forced disks: volume_number=%d", volume_number);
    return 0;
  }

  int *sp = new int[gndisks];
  memset(sp, 0, gndisks * sizeof(int));

  int i = curr_vol;
  while (size_in_blocks > 0) {
    if (gdisks[i]->free_space >= (sp[i] + blocks_per_vol)) {
      sp[i]          += blocks_per_vol;
      size_in_blocks -= blocks_per_vol;
      full_disks      = 0;
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
        DiskStripeBlock *p = gdisks[i]->create_volume(volume_number, sp[i], scheme);
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
  ReplaceablePtr<CacheHostTable>::ScopedWriter hosttable(&cache->hosttable);
  build_vol_hash_table(&hosttable->gen_host_rec);
  if (hosttable->m_numEntries != 0) {
    CacheHostMatcher *hm   = hosttable->getHostMatcher();
    CacheHostRecord *h_rec = hm->getDataArray();
    int h_rec_len          = hm->getNumElements();
    int i;
    for (i = 0; i < h_rec_len; i++) {
      build_vol_hash_table(&h_rec[i]);
    }
  }
}

// if generic_host_rec.vols == nullptr, what do we do???
Stripe *
Cache::key_to_vol(const CacheKey *key, const char *hostname, int host_len)
{
  ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(&this->hosttable);

  uint32_t h                      = (key->slice32(2) >> DIR_TAG_WIDTH) % VOL_HASH_TABLE_SIZE;
  unsigned short *hash_table      = hosttable->gen_host_rec.vol_hash_table;
  const CacheHostRecord *host_rec = &hosttable->gen_host_rec;

  if (hosttable->m_numEntries > 0 && host_len) {
    CacheHostResult res;
    hosttable->Match(hostname, host_len, &res);
    if (res.record) {
      unsigned short *host_hash_table = res.record->vol_hash_table;
      if (host_hash_table) {
        if (dbg_ctl_cache_hosting.on()) {
          char format_str[50];
          snprintf(format_str, sizeof(format_str), "Volume: %%xd for host: %%.%ds", host_len);
          Dbg(dbg_ctl_cache_hosting, format_str, res.record, hostname);
        }
        return res.record->vols[host_hash_table[h]];
      }
    }
  }
  if (hash_table) {
    if (dbg_ctl_cache_hosting.on()) {
      char format_str[50];
      snprintf(format_str, sizeof(format_str), "Generic volume: %%xd for host: %%.%ds", host_len);
      Dbg(dbg_ctl_cache_hosting, format_str, host_rec, hostname);
    }
    return host_rec->vols[hash_table[h]];
  } else {
    return host_rec->vols[0];
  }
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

  REC_EstablishStaticConfigInteger(cache_config_ram_cache_size, "proxy.config.cache.ram_cache.size");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.ram_cache.size = %" PRId64 " = %" PRId64 "Mb", cache_config_ram_cache_size,
      cache_config_ram_cache_size / (1024 * 1024));

  REC_EstablishStaticConfigInt32(cache_config_ram_cache_algorithm, "proxy.config.cache.ram_cache.algorithm");
  REC_EstablishStaticConfigInt32(cache_config_ram_cache_compress, "proxy.config.cache.ram_cache.compress");
  REC_EstablishStaticConfigInt32(cache_config_ram_cache_compress_percent, "proxy.config.cache.ram_cache.compress_percent");
  REC_ReadConfigInt32(cache_config_ram_cache_use_seen_filter, "proxy.config.cache.ram_cache.use_seen_filter");

  REC_EstablishStaticConfigInt32(cache_config_http_max_alts, "proxy.config.cache.limits.http.max_alts");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.limits.http.max_alts = %d", cache_config_http_max_alts);

  REC_EstablishStaticConfigInt32(cache_config_log_alternate_eviction, "proxy.config.cache.log.alternate.eviction");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.log.alternate.eviction = %d", cache_config_log_alternate_eviction);

  REC_EstablishStaticConfigInteger(cache_config_ram_cache_cutoff, "proxy.config.cache.ram_cache_cutoff");
  Dbg(dbg_ctl_cache_init, "cache_config_ram_cache_cutoff = %" PRId64 " = %" PRId64 "Mb", cache_config_ram_cache_cutoff,
      cache_config_ram_cache_cutoff / (1024 * 1024));

  REC_EstablishStaticConfigInt32(cache_config_permit_pinning, "proxy.config.cache.permit.pinning");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.permit.pinning = %d", cache_config_permit_pinning);

  REC_EstablishStaticConfigInt32(cache_config_dir_sync_frequency, "proxy.config.cache.dir.sync_frequency");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.dir.sync_frequency = %d", cache_config_dir_sync_frequency);

  REC_EstablishStaticConfigInt32(cache_config_select_alternate, "proxy.config.cache.select_alternate");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.select_alternate = %d", cache_config_select_alternate);

  REC_EstablishStaticConfigInt32(cache_config_max_doc_size, "proxy.config.cache.max_doc_size");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.max_doc_size = %d = %dMb", cache_config_max_doc_size,
      cache_config_max_doc_size / (1024 * 1024));

  REC_EstablishStaticConfigInt32(cache_config_mutex_retry_delay, "proxy.config.cache.mutex_retry_delay");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.mutex_retry_delay = %dms", cache_config_mutex_retry_delay);

  REC_EstablishStaticConfigInt32(cache_config_read_while_writer_max_retries, "proxy.config.cache.read_while_writer.max_retries");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.read_while_writer.max_retries = %d", cache_config_read_while_writer_max_retries);

  REC_EstablishStaticConfigInt32(cache_read_while_writer_retry_delay, "proxy.config.cache.read_while_writer_retry.delay");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.read_while_writer_retry.delay = %dms", cache_read_while_writer_retry_delay);

  REC_EstablishStaticConfigInt32(cache_config_hit_evacuate_percent, "proxy.config.cache.hit_evacuate_percent");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.hit_evacuate_percent = %d", cache_config_hit_evacuate_percent);

  REC_EstablishStaticConfigInt32(cache_config_hit_evacuate_size_limit, "proxy.config.cache.hit_evacuate_size_limit");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.hit_evacuate_size_limit = %d", cache_config_hit_evacuate_size_limit);

  REC_EstablishStaticConfigInt32(cache_config_force_sector_size, "proxy.config.cache.force_sector_size");

  ink_assert(REC_RegisterConfigUpdateFunc("proxy.config.cache.target_fragment_size", FragmentSizeUpdateCb, nullptr) !=
             REC_ERR_FAIL);
  REC_ReadConfigInt32(cache_config_target_fragment_size, "proxy.config.cache.target_fragment_size");

  if (cache_config_target_fragment_size == 0 || cache_config_target_fragment_size - sizeof(Doc) > MAX_FRAG_SIZE) {
    cache_config_target_fragment_size = DEFAULT_TARGET_FRAGMENT_SIZE;
  }

  REC_EstablishStaticConfigInt32(cache_config_max_disk_errors, "proxy.config.cache.max_disk_errors");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.max_disk_errors = %d", cache_config_max_disk_errors);

  REC_EstablishStaticConfigInt32(cache_config_agg_write_backlog, "proxy.config.cache.agg_write_backlog");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.agg_write_backlog = %d", cache_config_agg_write_backlog);

  REC_EstablishStaticConfigInt32(cache_config_enable_checksum, "proxy.config.cache.enable_checksum");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.enable_checksum = %d", cache_config_enable_checksum);

  REC_EstablishStaticConfigInt32(cache_config_alt_rewrite_max_size, "proxy.config.cache.alt_rewrite_max_size");
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.alt_rewrite_max_size = %d", cache_config_alt_rewrite_max_size);

  REC_EstablishStaticConfigInt32(cache_config_read_while_writer, "proxy.config.cache.enable_read_while_writer");
  cache_config_read_while_writer = validate_rww(cache_config_read_while_writer);
  REC_RegisterConfigUpdateFunc("proxy.config.cache.enable_read_while_writer", update_cache_config, nullptr);
  Dbg(dbg_ctl_cache_init, "proxy.config.cache.enable_read_while_writer = %d", cache_config_read_while_writer);

  register_cache_stats(&cache_rsb, "proxy.process.cache");

  REC_ReadConfigInteger(cacheProcessor.wait_for_cache, "proxy.config.http.wait_for_cache");

  Result result = theCacheStore.read_config();
  if (result.failed()) {
    Fatal("Failed to read cache configuration %s: %s", ts::filename::STORAGE, result.message());
  }
}

//----------------------------------------------------------------------------
Action *
CacheProcessor::open_read(Continuation *cont, const HttpCacheKey *key, CacheHTTPHdr *request, const HttpConfigAccessor *params,
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
// Note: this should not be called from the cluster processor, or bad
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
