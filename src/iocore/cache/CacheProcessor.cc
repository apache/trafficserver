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
#include "iocore/cache/CacheDefs.h"
#include "iocore/cache/Store.h"
#include "P_CacheDisk.h"
#include "P_CacheInternal.h"
#include "StripeSM.h"
#include "Stripe.h"

// Must be included after P_CacheInternal.h.
#include "P_CacheHosting.h"

#include "iocore/eventsystem/Action.h"
#include "iocore/eventsystem/Continuation.h"

#include "../../records/P_RecProcess.h"

#include "tscore/Diags.h"
#include "tscore/Filenames.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_platform.h"
#include "tscore/Layout.h"
#include "tscore/List.h"

#include "tsutil/DbgCtl.h"
#include "tsutil/Metrics.h"

#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

static void    CachePeriodicMetricsUpdate();
static int64_t cache_bytes_used(int index);
static void    rebuild_host_table(Cache *cache);
static int     cmprtable(const void *aa, const void *bb);
static void    persist_bad_disks();
int            cplist_reconfigure();
void           cplist_init();
void           register_cache_stats(CacheStatsBlock *rsb, const std::string &prefix);
static void    cplist_update();
static int     create_volume(int volume_number, off_t size_in_blocks, CacheType scheme, CacheVol *cp);
static int     fillExclusiveDisks(CacheVol *cp);

static size_t DEFAULT_RAM_CACHE_MULTIPLIER = 10; // I.e. 10x 1MB per 1GB of disk.

// Configuration
extern int64_t cache_config_ram_cache_size;
extern int     cache_config_ram_cache_algorithm;
extern int64_t cache_config_ram_cache_cutoff;
extern int     cache_config_persist_bad_disks;

// Globals

extern CacheDisk                     **gdisks;
extern int                             gndisks;
static std::atomic<int>                initialize_disk;
extern Store                           theCacheStore;
CacheInitState                         CacheProcessor::initialized          = CacheInitState::INITIALIZING;
uint32_t                               CacheProcessor::cache_ready          = 0;
int                                    CacheProcessor::start_done           = 0;
bool                                   CacheProcessor::clear                = false;
bool                                   CacheProcessor::fix                  = false;
bool                                   CacheProcessor::check                = false;
int                                    CacheProcessor::start_internal_flags = 0;
int                                    CacheProcessor::auto_clear_flag      = 0;
CacheProcessor                         cacheProcessor;
extern std::unordered_set<std::string> known_bad_disks;

namespace
{

DbgCtl dbg_ctl_cache_init{"cache_init"};
DbgCtl dbg_ctl_cache_remove{"cache_remove"};
DbgCtl dbg_ctl_cache_hosting{"cache_hosting"};
DbgCtl dbg_ctl_ram_cache{"ram_cache"};

} // end anonymous namespace

// Global list of the volumes created
extern Queue<CacheVol> cp_list;
extern int             cp_list_len;
extern ConfigVolumes   config_volumes;

static const int DEFAULT_CACHE_OPTIONS = (O_RDWR);

// explicit pair for random table in build_vol_hash_table
struct rtable_pair {
  unsigned int rval; ///< relative value, used to sort.
  unsigned int idx;  ///< volume mapping table index.
};

int
CacheProcessor::start(int, size_t)
{
  RecRegNewSyncStatSync(CachePeriodicMetricsUpdate);

  return start_internal(0);
}

void
CachePeriodicMetricsUpdate()
{
  int64_t total_sum = 0;

  // Make sure the bytes_used per volume is always reset to zero, this can update the
  // volume metric more than once (once per disk). This happens once every sync
  // period (5s), and nothing else modifies these metrics.
  for (int i = 0; i < gnstripes; ++i) {
    ts::Metrics::Gauge::store(gstripes[i]->cache_vol->vol_rsb.bytes_used, 0);
  }

  if (cacheProcessor.initialized == CacheInitState::INITIALIZED) {
    for (int i = 0; i < gnstripes; ++i) {
      StripeSM *v    = gstripes[i];
      int64_t   used = cache_bytes_used(i);

      ts::Metrics::Gauge::increment(v->cache_vol->vol_rsb.bytes_used, used); // This assumes they start at zero
      total_sum += used;
    }

    // Also update the global (not per volume) metrics
    int64_t total = ts::Metrics::Gauge::load(cache_rsb.bytes_total);

    ts::Metrics::Gauge::store(cache_rsb.bytes_used, total_sum);
    ts::Metrics::Gauge::store(cache_rsb.percent_full, total ? (total_sum * 100) / total : 0);
  }
}

// ToDo: This gets called as part of librecords collection continuation, probably change this later.
inline int64_t
cache_bytes_used(int index)
{
  if (!DISK_BAD(gstripes[index]->disk)) {
    if (!gstripes[index]->directory.header->cycle) {
      return gstripes[index]->directory.header->write_pos - gstripes[index]->start;
    } else {
      return gstripes[index]->len - gstripes[index]->dirlen() - EVACUATION_SIZE;
    }
  }

  return 0;
}

int
CacheProcessor::start_internal(int flags)
{
  start_internal_flags = flags;
  clear                = !!(flags & PROCESSOR_RECONFIGURE) || auto_clear_flag;
  fix                  = !!(flags & PROCESSOR_FIX);
  check                = (flags & PROCESSOR_CHECK) != 0;
  start_done           = 0;

  /* Read the config file and create the data structures corresponding to the file. */
  gndisks = theCacheStore.n_spans;
  gdisks  = static_cast<CacheDisk **>(ats_malloc(gndisks * sizeof(CacheDisk *)));

  // Temporaries to carry values between loops
  char **paths = static_cast<char **>(alloca(sizeof(char *) * gndisks));
  memset(paths, 0, sizeof(char *) * gndisks);
  int *fds = static_cast<int *>(alloca(sizeof(int) * gndisks));
  memset(fds, 0, sizeof(int) * gndisks);
  int *sector_sizes = static_cast<int *>(alloca(sizeof(int) * gndisks));
  memset(sector_sizes, 0, sizeof(int) * gndisks);
  Span **spans = static_cast<Span **>(alloca(sizeof(Span *) * gndisks));
  memset(spans, 0, sizeof(Span *) * gndisks);

  gndisks = 0;
  ink_aio_set_err_callback(new AIO_failure_handler());

  config_volumes.read_config_file();

  /*
   create CacheDisk objects for each span in the configuration file and store in gdisks
   */
  for (unsigned i = 0; i < theCacheStore.n_spans; i++) {
    Span *span = theCacheStore.spans[i];
    int   opts = DEFAULT_CACHE_OPTIONS;

    if (!paths[gndisks]) {
      paths[gndisks] = static_cast<char *>(alloca(PATH_NAME_MAX));
    }
    ink_strlcpy(paths[gndisks], span->pathname, PATH_NAME_MAX);
    if (!span->file_pathname) {
      ink_strlcat(paths[gndisks], "/cache.db", PATH_NAME_MAX);
      opts |= O_CREAT;
    }

    // Check if the disk is known to be bad.
    if (cache_config_persist_bad_disks && !known_bad_disks.empty()) {
      if (known_bad_disks.find(paths[gndisks]) != known_bad_disks.end()) {
        Warning("%s is a known bad disk.  Skipping.", paths[gndisks]);
        ts::Metrics::Gauge::increment(cache_rsb.span_offline);
        continue;
      }
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
    int64_t blocks = span->blocks;

    if (fd < 0 && (opts & O_CREAT)) { // Try without O_DIRECT if this is a file on filesystem, e.g. tmpfs.
#ifdef AIO_FAULT_INJECTION
      fd = aioFaultInjection.open(paths[gndisks], DEFAULT_CACHE_OPTIONS | O_CREAT, 0644);
#else
      fd = open(paths[gndisks], DEFAULT_CACHE_OPTIONS | O_CREAT, 0644);
#endif
    }

    if (fd >= 0) {
      bool diskok = true;
      if (!span->file_pathname) {
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
        int sector_size = span->hw_sector_size;

        CacheDisk *cache_disk = new CacheDisk();
        if (check) {
          cache_disk->read_only_p = true;
        }
        cache_disk->forced_volume_num = span->forced_volume_num;
        if (span->hash_base_string) {
          cache_disk->hash_base_string = ats_strdup(span->hash_base_string);
        }

        if (sector_size < cache_config_force_sector_size) {
          sector_size = cache_config_force_sector_size;
        }

        // It's actually common that the hardware I/O size is larger than the store block size as
        // storage systems increasingly want larger I/Os. For example, on macOS, the filesystem
        // block size is always reported as 1MB.
        if (span->hw_sector_size <= 0 || sector_size > STORE_BLOCK_SIZE) {
          Note("resetting hardware sector size from %d to %d", sector_size, STORE_BLOCK_SIZE);
          sector_size = STORE_BLOCK_SIZE;
        }

        gdisks[gndisks]       = cache_disk;
        sector_sizes[gndisks] = sector_size;
        fds[gndisks]          = fd;
        spans[gndisks]        = span;
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
    CacheProcessor::initialized = CacheInitState::FAILED;
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
  } else if (this->waitForCache() == 3 && static_cast<unsigned int>(gndisks) < theCacheStore.n_spans_in_config) {
    CacheProcessor::initialized = CacheInitState::FAILED;
    if (cb_after_init) {
      cb_after_init();
    }
    Emergency("Cache initialization failed - only %d out of %d disks were valid and all were required.", gndisks,
              theCacheStore.n_spans_in_config);
  } else if (this->waitForCache() == 2 && static_cast<unsigned int>(gndisks) < theCacheStore.n_spans_in_config) {
    Warning("Cache initialization incomplete - only %d out of %d disks were valid.", gndisks, theCacheStore.n_spans_in_config);
  }

  // If we got here, we have enough disks to proceed
  for (int j = 0; j < gndisks; j++) {
    Span *sd = spans[j];
    ink_release_assert(spans[j] != nullptr); // Defeat clang-analyzer
    off_t   skip   = ROUND_TO_STORE_BLOCK((sd->offset < START_POS ? START_POS + sd->alignment : sd->offset));
    int64_t blocks = sd->blocks - (skip >> STORE_BLOCK_SHIFT);
    gdisks[j]->open(paths[j], blocks, skip, sector_sizes[j], fds[j], clear);

    Dbg(dbg_ctl_cache_hosting, "Disk: %d:%s, blocks: %" PRId64 "", gndisks, paths[j], blocks);
  }

  return 0;
}

void
CacheProcessor::stop()
{
}

int
CacheProcessor::dir_check(bool /* afix ATS_UNUSED */)
{
  for (int i = 0; i < gnstripes; i++) {
    gstripes[i]->dir_check();
  }
  return 0;
}

Action *
CacheProcessor::lookup(Continuation *cont, const CacheKey *key, CacheFragType frag_type, std::string_view hostname)
{
  return caches[frag_type]->lookup(cont, key, frag_type, hostname);
}

Action *
CacheProcessor::open_read(Continuation *cont, const CacheKey *key, CacheFragType frag_type, std::string_view hostname)
{
  return caches[frag_type]->open_read(cont, key, frag_type, hostname);
}

Action *
CacheProcessor::open_write(Continuation *cont, CacheKey *key, CacheFragType frag_type, int expected_size ATS_UNUSED, int options,
                           time_t pin_in_cache, std::string_view hostname)
{
  return caches[frag_type]->open_write(cont, key, frag_type, options, pin_in_cache, hostname);
}

Action *
CacheProcessor::remove(Continuation *cont, const CacheKey *key, CacheFragType frag_type, std::string_view hostname)
{
  Dbg(dbg_ctl_cache_remove, "[CacheProcessor::remove] Issuing cache delete for %u", cache_hash(*key));
  return caches[frag_type]->remove(cont, key, frag_type, hostname);
}

Action *
CacheProcessor::scan(Continuation *cont, std::string_view hostname, int KB_per_second)
{
  return caches[CACHE_FRAG_TYPE_HTTP]->scan(cont, hostname, KB_per_second);
}

Action *
CacheProcessor::lookup(Continuation *cont, const HttpCacheKey *key, CacheFragType frag_type)
{
  return lookup(cont, &key->hash, frag_type,
                std::string_view{key->hostname, static_cast<std::string_view::size_type>(key->hostlen)});
}

Action *
CacheProcessor::open_read(Continuation *cont, const HttpCacheKey *key, CacheHTTPHdr *request, const HttpConfigAccessor *params,
                          CacheFragType type)
{
  return caches[type]->open_read(cont, &key->hash, request, params, type,
                                 std::string_view{key->hostname, static_cast<std::string_view::size_type>(key->hostlen)});
}

Action *
CacheProcessor::open_write(Continuation *cont, const HttpCacheKey *key, CacheHTTPInfo *old_info, time_t pin_in_cache,
                           CacheFragType type)
{
  return caches[type]->open_write(cont, &key->hash, old_info, pin_in_cache, type,
                                  std::string_view{key->hostname, static_cast<std::string_view::size_type>(key->hostlen)});
}

//----------------------------------------------------------------------------
// Note: this should not be called from the cluster processor, or bad
// recursion could occur. This is merely a convenience wrapper.
Action *
CacheProcessor::remove(Continuation *cont, const HttpCacheKey *key, CacheFragType frag_type)
{
  return caches[frag_type]->remove(cont, &key->hash, frag_type,
                                   std::string_view{key->hostname, static_cast<std::string_view::size_type>(key->hostlen)});
}

/** Set the state of a disk programmatically.
 */
bool
CacheProcessor::mark_storage_offline(CacheDisk *d, ///< Target disk
                                     bool       admin)
{
  bool     zret; // indicates whether there's any online storage left.
  int      p;
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

  for (p = 0; p < gnstripes; p++) {
    if (d->fd == gstripes[p]->fd) {
      total_dir_delete   += gstripes[p]->directory.entries();
      used_dir_delete    += dir_entries_used(gstripes[p]);
      total_bytes_delete += gstripes[p]->len - gstripes[p]->dirlen();
    }
  }

  ts::Metrics::Gauge::decrement(cache_rsb.bytes_total, total_bytes_delete);
  ts::Metrics::Gauge::decrement(cache_rsb.direntries_total, total_dir_delete);
  ts::Metrics::Gauge::decrement(cache_rsb.direntries_used, used_dir_delete);

  /* Update the span metrics, if failing then move the span from "failing" to "offline" bucket
   * if operator took it offline, move it from "online" to "offline" bucket */
  ts::Metrics::Gauge::decrement(admin ? cache_rsb.span_online : cache_rsb.span_failing);
  ts::Metrics::Gauge::increment(cache_rsb.span_offline);

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

  if (cache_config_persist_bad_disks) {
    known_bad_disks.insert(d->path);
    persist_bad_disks();
  }

  return zret;
}

void
rebuild_host_table(Cache *cache)
{
  ReplaceablePtr<CacheHostTable>::ScopedWriter hosttable(&cache->hosttable);
  build_vol_hash_table(&hosttable->gen_host_rec);
  if (hosttable->m_numEntries != 0) {
    CacheHostMatcher *hm        = hosttable->getHostMatcher();
    CacheHostRecord  *h_rec     = hm->getDataArray();
    int               h_rec_len = hm->getNumElements();
    int               i;
    for (i = 0; i < h_rec_len; i++) {
      build_vol_hash_table(&h_rec[i]);
    }
  }
}

void
build_vol_hash_table(CacheHostRecord *cp)
{
  int           num_vols = cp->num_vols;
  unsigned int *mapping  = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  StripeSM    **p        = static_cast<StripeSM **>(ats_malloc(sizeof(StripeSM *) * num_vols));

  memset(mapping, 0, num_vols * sizeof(unsigned int));
  memset(p, 0, num_vols * sizeof(StripeSM *));
  uint64_t total    = 0;
  int      bad_vols = 0;
  int      map      = 0;
  uint64_t used     = 0;
  // initialize number of elements per vol
  for (int i = 0; i < num_vols; i++) {
    if (DISK_BAD(cp->stripes[i]->disk)) {
      bad_vols++;
      continue;
    }
    mapping[map]  = i;
    p[map++]      = cp->stripes[i];
    total        += (cp->stripes[i]->len >> STORE_BLOCK_SHIFT);
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

  unsigned int   *forvol = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned int   *gotvol = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned int   *rnd    = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned short *ttable = static_cast<unsigned short *>(ats_malloc(sizeof(unsigned short) * STRIPE_HASH_TABLE_SIZE));
  unsigned short *old_table;
  unsigned int   *rtable_entries = static_cast<unsigned int *>(ats_malloc(sizeof(unsigned int) * num_vols));
  unsigned int    rtable_size    = 0;

  // estimate allocation
  for (int i = 0; i < num_vols; i++) {
    forvol[i]          = (STRIPE_HASH_TABLE_SIZE * (p[i]->len >> STORE_BLOCK_SHIFT)) / total;
    used              += forvol[i];
    rtable_entries[i]  = p[i]->len / STRIPE_HASH_ALLOC_SIZE;
    rtable_size       += rtable_entries[i];
    gotvol[i]          = 0;
  }
  // spread around the excess
  int extra = STRIPE_HASH_TABLE_SIZE - used;
  for (int i = 0; i < extra; i++) {
    forvol[i % num_vols]++;
  }
  // seed random number generator
  for (int i = 0; i < num_vols; i++) {
    uint64_t x = p[i]->hash_id.fold();
    rnd[i]     = static_cast<unsigned int>(x);
  }
  // initialize table to "empty"
  for (int i = 0; i < STRIPE_HASH_TABLE_SIZE; i++) {
    ttable[i] = STRIPE_HASH_EMPTY;
  }
  // generate random numbers proportional to allocation
  rtable_pair *rtable = static_cast<rtable_pair *>(ats_malloc(sizeof(rtable_pair) * rtable_size));
  unsigned int rindex = 0;
  for (int i = 0; i < num_vols; i++) {
    for (int j = 0; j < static_cast<int>(rtable_entries[i]); j++) {
      rtable[rindex].rval = next_rand(&rnd[i]);
      rtable[rindex].idx  = i;
      rindex++;
    }
  }
  ink_assert(rindex == rtable_size);
  // sort (rand #, vol $ pairs)
  qsort(rtable, rtable_size, sizeof(rtable_pair), cmprtable);
  unsigned int width = (1LL << 32) / STRIPE_HASH_TABLE_SIZE;
  unsigned int pos; // target position to allocate
  // select vol with closest random number for each bucket
  int i = 0; // index moving through the random numbers
  for (int j = 0; j < STRIPE_HASH_TABLE_SIZE; j++) {
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

// comparison operator for random table in build_vol_hash_table
// sorts based on the randomly assigned rval
int
cmprtable(const void *aa, const void *bb)
{
  const rtable_pair *a = static_cast<const rtable_pair *>(aa);
  const rtable_pair *b = static_cast<const rtable_pair *>(bb);
  if (a->rval < b->rval) {
    return -1;
  }
  if (a->rval > b->rval) {
    return 1;
  }
  return 0;
}

void
persist_bad_disks()
{
  std::filesystem::path localstatedir{Layout::get()->localstatedir};
  std::filesystem::path bad_disks_path{localstatedir / ts::filename::BAD_DISKS};
  std::error_code       ec;
  std::filesystem::create_directories(bad_disks_path.parent_path(), ec);
  if (ec) {
    Error("Error creating directory for bad disks file: %s (%s)", bad_disks_path.c_str(), ec.message().c_str());
    return;
  }

  std::fstream bad_disks_file{bad_disks_path.c_str(), bad_disks_file.out};
  if (bad_disks_file.good()) {
    for (const std::string &path : known_bad_disks) {
      bad_disks_file << path << std::endl;
      if (bad_disks_file.fail()) {
        Error("Error writing known bad disks file: %s", bad_disks_path.c_str());
        break;
      }
    }
  } else {
    Error("Failed to open file to persist bad disks: %s", bad_disks_path.c_str());
  }
}

CacheDisk *
CacheProcessor::find_by_path(std::string_view path)
{
  if (CacheInitState::INITIALIZED == initialized) {
    for (int i = 0; i < gndisks; ++i) {
      if (0 == strncmp(path.data(), gdisks[i]->path, path.length())) {
        return gdisks[i];
      }
    }
  }

  return nullptr;
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

CacheInitState
CacheProcessor::IsCacheEnabled()
{
  return CacheProcessor::initialized;
}

bool
CacheProcessor::IsCacheReady(CacheFragType type)
{
  if (IsCacheEnabled() != CacheInitState::INITIALIZED) {
    return false;
  }
  return static_cast<bool>(cache_ready & (1 << type));
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
      CacheProcessor::initialized = CacheInitState::FAILED;
      if (cb_after_init) {
        cb_after_init();
      }
      Emergency("Cache initialization failed - only %d of %d disks were available.", gndisks, theCacheStore.n_spans_in_config);
    } else if (this->waitForCache() == 2) {
      Warning("Cache initialization incomplete - only %d of %d disks were available.", gndisks, theCacheStore.n_spans_in_config);
    }
  }

  /* Practically just took all bad_disks offline so update the stats. */
  // ToDo: These don't get update on the per-volume metrics :-/
  ts::Metrics::Gauge::store(cache_rsb.span_offline, bad_disks);
  ts::Metrics::Gauge::decrement(cache_rsb.span_failing, bad_disks);
  ts::Metrics::Gauge::store(cache_rsb.span_online, gndisks);

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
    gnstripes = 0;
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

  gstripes = static_cast<StripeSM **>(ats_malloc(gnstripes * sizeof(StripeSM *)));
  memset(gstripes, 0, gnstripes * sizeof(StripeSM *));
  gnstripes = 0;
  for (i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    if (dbg_ctl_cache_hosting.on()) {
      int j;
      DbgPrint(dbg_ctl_cache_hosting, "Disk: %d:%s: Stripe Blocks: %u: Free space: %" PRIu64, i, d->path,
               d->header->num_diskvol_blks, d->free_space);
      for (j = 0; j < static_cast<int>(d->header->num_volumes); j++) {
        DbgPrint(dbg_ctl_cache_hosting, "\tStripe: %d Size: %" PRIu64, d->disk_stripes[j]->vol_number, d->disk_stripes[j]->size);
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
    theCache->scheme = CacheType::HTTP;
    theCache->open(clear, fix);
    return;
  }
  if (config_volumes.num_http_volumes != 0) {
    theCache         = new Cache();
    theCache->scheme = CacheType::HTTP;
    theCache->open(clear, fix);
  }
}

int
cplist_reconfigure()
{
  int64_t    size;
  int        volume_number;
  off_t      size_in_blocks;
  ConfigVol *config_vol;

  gnstripes = 0;
  if (config_volumes.num_volumes == 0) {
    /* only the http cache */
    CacheVol *cp     = new CacheVol();
    cp->vol_number   = 0;
    cp->scheme       = CacheType::HTTP;
    cp->disk_stripes = static_cast<DiskStripe **>(ats_malloc(gndisks * sizeof(DiskStripe *)));
    memset(cp->disk_stripes, 0, gndisks * sizeof(DiskStripe *));
    cp_list.enqueue(cp);
    cp_list_len++;
    for (int i = 0; i < gndisks; i++) {
      if (gdisks[i]->header->num_volumes != 1 || gdisks[i]->disk_stripes[0]->vol_number != 0) {
        /* The user had created several volumes before - clear the disk
           and create one volume for http */
        Note("Clearing Disk: %s", gdisks[i]->path);
        gdisks[i]->delete_all_volumes();
      }
      if (gdisks[i]->cleared) {
        uint64_t free_space = gdisks[i]->free_space * STORE_BLOCK_SIZE;
        int      vols       = (free_space / MAX_STRIPE_SIZE) + 1;
        for (int p = 0; p < vols; p++) {
          off_t b = gdisks[i]->free_space / (vols - p);
          Dbg(dbg_ctl_cache_hosting, "blocks = %" PRId64, (int64_t)b);
          DiskStripeBlock *dpb = gdisks[i]->create_volume(0, b, CacheType::HTTP);
          ink_assert(dpb && dpb->len == (uint64_t)b);
        }
        ink_assert(gdisks[i]->free_space == 0);
      }

      ink_assert(gdisks[i]->header->num_volumes == 1);
      DiskStripe **dp      = gdisks[i]->disk_stripes;
      gnstripes           += dp[0]->num_volblocks;
      cp->size            += dp[0]->size;
      cp->num_vols        += dp[0]->num_volblocks;
      cp->disk_stripes[i]  = dp[0];
    }
  } else {
    for (int i = 0; i < gndisks; i++) {
      if (gdisks[i]->header->num_volumes == 1 && gdisks[i]->disk_stripes[0]->vol_number == 0) {
        /* The user had created several volumes before - clear the disk
           and create one volume for http */
        Note("Clearing Disk: %s", gdisks[i]->path);
        gdisks[i]->delete_all_volumes();
      }
    }

    /* change percentages in the config partitions to absolute value */
    off_t tot_space_in_blks = 0;
    off_t blocks_per_vol    = STORE_BLOCKS_PER_STRIPE;
    /* sum up the total space available on all the disks.
       round down the space to 128 megabytes */
    for (int i = 0; i < gndisks; i++) {
      // Exclude exclusive disks (with forced volumes) from the following total space calculation,
      // in such a way forced volumes will not impact volume percentage calculations.
      if (-1 == gdisks[i]->forced_volume_num) {
        tot_space_in_blks += (gdisks[i]->num_usable_blocks / blocks_per_vol) * blocks_per_vol;
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
                static_cast<int64_t>(config_vol->size), 128);
        Warning("volume %d is not created", config_vol->number);
      }
      Dbg(dbg_ctl_cache_hosting, "Volume: %d Size: %" PRId64 " Ramcache: %d", config_vol->number,
          static_cast<int64_t>(config_vol->size), config_vol->ramcache_enabled);
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
        gnstripes += config_vol->cachep->num_vols;
        continue;
      }

      if (!config_vol->cachep) {
        // we did not find a corresponding entry in cache vol...create one

        CacheVol *new_cp     = new CacheVol();
        new_cp->disk_stripes = static_cast<DiskStripe **>(ats_malloc(gndisks * sizeof(DiskStripe *)));
        memset(new_cp->disk_stripes, 0, gndisks * sizeof(DiskStripe *));
        if (create_volume(config_vol->number, size_in_blocks, config_vol->scheme, new_cp)) {
          ats_free(new_cp->disk_stripes);
          new_cp->disk_stripes = nullptr;
          delete new_cp;
          return -1;
        }
        cp_list.enqueue(new_cp);
        cp_list_len++;
        config_vol->cachep  = new_cp;
        gnstripes          += new_cp->num_vols;
        continue;
      }
      //    else
      CacheVol *cp = config_vol->cachep;
      ink_assert(cp->size <= size_in_blocks);
      if (cp->size == size_in_blocks) {
        gnstripes += cp->num_vols;
        continue;
      }
      // else the size is greater...
      /* search the cp_list */

      std::vector<int> sorted_vols(gndisks);
      std::iota(sorted_vols.begin(), sorted_vols.end(), 0);
      for (int i = 0; i < gndisks - 1; i++) {
        int smallest     = sorted_vols[i];
        int smallest_ndx = i;
        for (int j = i + 1; j < gndisks; j++) {
          int         curr        = sorted_vols[j];
          DiskStripe *disk_stripe = cp->disk_stripes[curr];
          if (gdisks[curr]->cleared) {
            ink_assert(!disk_stripe);
            // disks that are cleared should be filled first
            smallest     = curr;
            smallest_ndx = j;
          } else if (!disk_stripe && cp->disk_stripes[smallest]) {
            smallest     = curr;
            smallest_ndx = j;
          } else if (disk_stripe && cp->disk_stripes[smallest] && (disk_stripe->size < cp->disk_stripes[smallest]->size)) {
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
        ink_assert(cp->disk_stripes[sorted_vols[gndisks - 1]]);
        int largest_vol = cp->disk_stripes[sorted_vols[gndisks - 1]]->size;

        /* allocate storage on new disk. Find the difference
           between the biggest volume on any disk and
           the volume on this disk and try to make
           them equal */
        int64_t size_diff = (cp->disk_stripes[disk_no]) ? largest_vol - cp->disk_stripes[disk_no]->size : largest_vol;
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
            if (!cp->disk_stripes[disk_no]) {
              cp->disk_stripes[disk_no] = gdisks[disk_no]->get_diskvol(volume_number);
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

      if (size_to_alloc) {
        if (create_volume(volume_number, size_to_alloc, cp->scheme, cp)) {
          return -1;
        }
      }
      gnstripes += cp->num_vols;
    }
  }

  ts::Metrics::Gauge::store(cache_rsb.stripes, gnstripes);

  return 0;
}

void
cplist_init()
{
  cp_list_len = 0;
  for (int i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    ink_assert(d != nullptr);
    DiskStripe **dp = d->disk_stripes;
    for (unsigned int j = 0; j < d->header->num_volumes; j++) {
      ink_assert(dp[j]->dpb_queue.head);
      CacheVol *p = cp_list.head;
      while (p) {
        if (p->vol_number == dp[j]->vol_number) {
          ink_assert(p->scheme == static_cast<CacheType>(dp[j]->dpb_queue.head->b->type));
          p->size            += dp[j]->size;
          p->num_vols        += dp[j]->num_volblocks;
          p->disk_stripes[i]  = dp[j];
          break;
        }
        p = p->link.next;
      }
      if (!p) {
        // did not find a volume in the cache vol list...create
        // a new one
        CacheVol *new_p     = new CacheVol();
        new_p->vol_number   = dp[j]->vol_number;
        new_p->num_vols     = dp[j]->num_volblocks;
        new_p->size         = dp[j]->size;
        new_p->scheme       = static_cast<CacheType>(dp[j]->dpb_queue.head->b->type);
        new_p->disk_stripes = static_cast<DiskStripe **>(ats_malloc(gndisks * sizeof(DiskStripe *)));
        memset(new_p->disk_stripes, 0, gndisks * sizeof(DiskStripe *));
        new_p->disk_stripes[i] = dp[j];
        cp_list.enqueue(new_p);
        cp_list_len++;
      }
    }
  }
}

// Register Stats, this handles both the global cache metrics, as well as the per volume metrics.
void
register_cache_stats(CacheStatsBlock *rsb, const std::string &prefix)
{
  // These are special, in that we have 7 x 3 metrics here in a structure based on cache operation done
  rsb->status[static_cast<int>(CacheOpType::Lookup)].active    = ts::Metrics::Gauge::createPtr(prefix + ".lookup.active");
  rsb->status[static_cast<int>(CacheOpType::Lookup)].success   = ts::Metrics::Counter::createPtr(prefix + ".lookup.success");
  rsb->status[static_cast<int>(CacheOpType::Lookup)].failure   = ts::Metrics::Counter::createPtr(prefix + ".lookup.failure");
  rsb->status[static_cast<int>(CacheOpType::Read)].active      = ts::Metrics::Gauge::createPtr(prefix + ".read.active");
  rsb->status[static_cast<int>(CacheOpType::Read)].success     = ts::Metrics::Counter::createPtr(prefix + ".read.success");
  rsb->status[static_cast<int>(CacheOpType::Read)].failure     = ts::Metrics::Counter::createPtr(prefix + ".read.failure");
  rsb->status[static_cast<int>(CacheOpType::Write)].active     = ts::Metrics::Gauge::createPtr(prefix + ".write.active");
  rsb->status[static_cast<int>(CacheOpType::Write)].success    = ts::Metrics::Counter::createPtr(prefix + ".write.success");
  rsb->status[static_cast<int>(CacheOpType::Write)].failure    = ts::Metrics::Counter::createPtr(prefix + ".write.failure");
  rsb->status[static_cast<int>(CacheOpType::Update)].active    = ts::Metrics::Gauge::createPtr(prefix + ".update.active");
  rsb->status[static_cast<int>(CacheOpType::Update)].success   = ts::Metrics::Counter::createPtr(prefix + ".update.success");
  rsb->status[static_cast<int>(CacheOpType::Update)].failure   = ts::Metrics::Counter::createPtr(prefix + ".update.failure");
  rsb->status[static_cast<int>(CacheOpType::Remove)].active    = ts::Metrics::Gauge::createPtr(prefix + ".remove.active");
  rsb->status[static_cast<int>(CacheOpType::Remove)].success   = ts::Metrics::Counter::createPtr(prefix + ".remove.success");
  rsb->status[static_cast<int>(CacheOpType::Remove)].failure   = ts::Metrics::Counter::createPtr(prefix + ".remove.failure");
  rsb->status[static_cast<int>(CacheOpType::Evacuate)].active  = ts::Metrics::Gauge::createPtr(prefix + ".evacuate.active");
  rsb->status[static_cast<int>(CacheOpType::Evacuate)].success = ts::Metrics::Counter::createPtr(prefix + ".evacuate.success");
  rsb->status[static_cast<int>(CacheOpType::Evacuate)].failure = ts::Metrics::Counter::createPtr(prefix + ".evacuate.failure");
  rsb->status[static_cast<int>(CacheOpType::Scan)].active      = ts::Metrics::Gauge::createPtr(prefix + ".scan.active");
  rsb->status[static_cast<int>(CacheOpType::Scan)].success     = ts::Metrics::Counter::createPtr(prefix + ".scan.success");
  rsb->status[static_cast<int>(CacheOpType::Scan)].failure     = ts::Metrics::Counter::createPtr(prefix + ".scan.failure");

  // These are in an array of 1, 2 and 3+ fragment documents
  rsb->fragment_document_count[0] = ts::Metrics::Counter::createPtr(prefix + ".frags_per_doc.1");
  rsb->fragment_document_count[1] = ts::Metrics::Counter::createPtr(prefix + ".frags_per_doc.2");
  rsb->fragment_document_count[2] = ts::Metrics::Counter::createPtr(prefix + ".frags_per_doc.3+");

  // And then everything else
  rsb->bytes_used            = ts::Metrics::Gauge::createPtr(prefix + ".bytes_used");
  rsb->bytes_total           = ts::Metrics::Gauge::createPtr(prefix + ".bytes_total");
  rsb->stripes               = ts::Metrics::Gauge::createPtr(prefix + ".stripes");
  rsb->ram_cache_bytes_total = ts::Metrics::Gauge::createPtr(prefix + ".ram_cache.total_bytes");
  rsb->ram_cache_bytes       = ts::Metrics::Gauge::createPtr(prefix + ".ram_cache.bytes_used");
  rsb->ram_cache_hits        = ts::Metrics::Counter::createPtr(prefix + ".ram_cache.hits");
  rsb->ram_cache_misses      = ts::Metrics::Counter::createPtr(prefix + ".ram_cache.misses");
  rsb->pread_count           = ts::Metrics::Counter::createPtr(prefix + ".pread_count");
  rsb->percent_full          = ts::Metrics::Gauge::createPtr(prefix + ".percent_full");
  rsb->read_seek_fail        = ts::Metrics::Counter::createPtr(prefix + ".read.seek.failure");
  rsb->read_invalid          = ts::Metrics::Counter::createPtr(prefix + ".read.invalid");
  rsb->write_backlog_failure = ts::Metrics::Counter::createPtr(prefix + ".write.backlog.failure");
  rsb->direntries_total      = ts::Metrics::Gauge::createPtr(prefix + ".direntries.total");
  rsb->direntries_used       = ts::Metrics::Gauge::createPtr(prefix + ".direntries.used");
  rsb->directory_collision   = ts::Metrics::Counter::createPtr(prefix + ".directory_collision");
  rsb->read_busy_success     = ts::Metrics::Counter::createPtr(prefix + ".read_busy.success");
  rsb->read_busy_failure     = ts::Metrics::Counter::createPtr(prefix + ".read_busy.failure");
  rsb->write_bytes           = ts::Metrics::Counter::createPtr(prefix + ".write_bytes_stat");
  rsb->hdr_vector_marshal    = ts::Metrics::Counter::createPtr(prefix + ".vector_marshals");
  rsb->hdr_marshal           = ts::Metrics::Counter::createPtr(prefix + ".hdr_marshals");
  rsb->hdr_marshal_bytes     = ts::Metrics::Counter::createPtr(prefix + ".hdr_marshal_bytes");
  rsb->gc_bytes_evacuated    = ts::Metrics::Counter::createPtr(prefix + ".gc_bytes_evacuated");
  rsb->gc_frags_evacuated    = ts::Metrics::Counter::createPtr(prefix + ".gc_frags_evacuated");
  rsb->directory_wrap        = ts::Metrics::Counter::createPtr(prefix + ".wrap_count");
  rsb->directory_sync_count  = ts::Metrics::Counter::createPtr(prefix + ".sync.count");
  rsb->directory_sync_bytes  = ts::Metrics::Counter::createPtr(prefix + ".sync.bytes");
  rsb->directory_sync_time   = ts::Metrics::Counter::createPtr(prefix + ".sync.time");
  rsb->span_errors_read      = ts::Metrics::Counter::createPtr(prefix + ".span.errors.read");
  rsb->span_errors_write     = ts::Metrics::Counter::createPtr(prefix + ".span.errors.write");
  rsb->span_failing          = ts::Metrics::Gauge::createPtr(prefix + ".span.failing");
  rsb->span_offline          = ts::Metrics::Gauge::createPtr(prefix + ".span.offline");
  rsb->span_online           = ts::Metrics::Gauge::createPtr(prefix + ".span.online");
}

void
cplist_update()
{
  /* go through cplist and delete volumes that are not in the volume.config */
  CacheVol  *cp = cp_list.head;
  ConfigVol *config_vol;

  while (cp) {
    for (config_vol = config_volumes.cp_queue.head; config_vol; config_vol = config_vol->link.next) {
      if (config_vol->number == cp->vol_number) {
        if (cp->scheme == config_vol->scheme) {
          cp->ramcache_enabled = config_vol->ramcache_enabled;
          cp->avg_obj_size     = config_vol->avg_obj_size;
          cp->fragment_size    = config_vol->fragment_size;
          config_vol->cachep   = cp;
        } else {
          /* delete this volume from all the disks */
          int d_no;
          int clearCV = 1;

          for (d_no = 0; d_no < gndisks; d_no++) {
            if (cp->disk_stripes[d_no]) {
              if (cp->disk_stripes[d_no]->disk->forced_volume_num == cp->vol_number) {
                clearCV            = 0;
                config_vol->cachep = cp;
              } else {
                cp->disk_stripes[d_no]->disk->delete_volume(cp->vol_number);
                cp->disk_stripes[d_no] = nullptr;
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
        if (cp->disk_stripes[d_no]) {
          cp->disk_stripes[d_no]->disk->delete_volume(cp->vol_number);
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
          new_cp->disk_stripes = static_cast<decltype(new_cp->disk_stripes)>(ats_malloc(gndisks * sizeof(DiskStripe *)));
          if (nullptr != new_cp->disk_stripes) {
            memset(new_cp->disk_stripes, 0, gndisks * sizeof(DiskStripe *));
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

// This is some really bad code, and needs to be rewritten!
int
create_volume(int volume_number, off_t size_in_blocks, CacheType scheme, CacheVol *cp)
{
  static int curr_vol       = 0; // FIXME: this will not reinitialize correctly
  off_t      to_create      = size_in_blocks;
  off_t      blocks_per_vol = STRIPE_BLOCK_SIZE >> STORE_BLOCK_SHIFT;
  int        full_disks     = 0;

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
        RecGetRecordString("proxy.config.cache.volume_filename", config_file, PATH_NAME_MAX);
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
      if (!cp->disk_stripes[i]) {
        cp->disk_stripes[i] = gdisks[i]->get_diskvol(volume_number);
      }
    }
  }
  delete[] sp;
  return 0;
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
      if (volume_number != gdisks[i]->disk_stripes[j]->vol_number) {
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

    int64_t          size_diff = gdisks[i]->num_usable_blocks;
    DiskStripeBlock *dpb;
    do {
      dpb = gdisks[i]->create_volume(volume_number, size_diff, cp->scheme);
      if (dpb) {
        if (!cp->disk_stripes[i]) {
          cp->disk_stripes[i] = gdisks[i]->get_diskvol(volume_number);
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

void
CacheProcessor::cacheInitialized()
{
  if (theCache == nullptr) {
    Dbg(dbg_ctl_cache_init, "theCache is nullptr");
    return;
  }

  if (theCache->ready == CacheInitState::INITIALIZING) {
    Dbg(dbg_ctl_cache_init, "theCache is initializing");
    return;
  }

  int caches_ready  = 0;
  int cache_init_ok = 0;
  /* allocate ram size in proportion to the disk space the
     volume occupies */
  int64_t total_size = 0; // count in HTTP & MIXT

  total_size += theCache->cache_size;
  Dbg(dbg_ctl_cache_init, "theCache, total_size = %" PRId64 " = %" PRId64 " MB", total_size,
      total_size / ((1024 * 1024) / STORE_BLOCK_SIZE));
  if (theCache->ready == CacheInitState::FAILED) {
    Dbg(dbg_ctl_cache_init, "failed to initialize the cache for http: cache disabled");
    Warning("failed to initialize the cache for http: cache disabled\n");
  } else {
    caches_ready                 = caches_ready | (1 << CACHE_FRAG_TYPE_HTTP);
    caches_ready                 = caches_ready | (1 << CACHE_FRAG_TYPE_NONE);
    caches[CACHE_FRAG_TYPE_HTTP] = theCache;
    caches[CACHE_FRAG_TYPE_NONE] = theCache;
  }

  // Update stripe version data.
  if (gnstripes) { // start with whatever the first stripe is.
    cacheProcessor.min_stripe_version = cacheProcessor.max_stripe_version = gstripes[0]->directory.header->version;
  }
  // scan the rest of the stripes.
  for (int i = 1; i < gnstripes; i++) {
    StripeSM *v = gstripes[i];
    if (v->directory.header->version < cacheProcessor.min_stripe_version) {
      cacheProcessor.min_stripe_version = v->directory.header->version;
    }
    if (cacheProcessor.max_stripe_version < v->directory.header->version) {
      cacheProcessor.max_stripe_version = v->directory.header->version;
    }
  }

  if (caches_ready) {
    Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized - caches_ready=0x%0X, gnvol=%d", (unsigned int)caches_ready,
        gnstripes.load());

    if (gnstripes) {
      // new ram_caches, with algorithm from the config
      for (int i = 0; i < gnstripes; i++) {
        switch (cache_config_ram_cache_algorithm) {
        default:
        case RAM_CACHE_ALGORITHM_CLFUS:
          gstripes[i]->ram_cache = new_RamCacheCLFUS();
          break;
        case RAM_CACHE_ALGORITHM_LRU:
          gstripes[i]->ram_cache = new_RamCacheLRU();
          break;
        }
      }

      int64_t http_ram_cache_size = 0;

      // let us calculate the Size
      if (cache_config_ram_cache_size == AUTO_SIZE_RAM_CACHE) {
        Dbg(dbg_ctl_cache_init, "cache_config_ram_cache_size == AUTO_SIZE_RAM_CACHE");
      } else {
        // we got configured memory size
        // TODO, should we check the available system memories, or you will
        //   OOM or swapout, that is not a good situation for the server
        Dbg(dbg_ctl_cache_init, "%" PRId64 " != AUTO_SIZE_RAM_CACHE", cache_config_ram_cache_size);
        http_ram_cache_size =
          static_cast<int64_t>((static_cast<double>(theCache->cache_size) / total_size) * cache_config_ram_cache_size);

        Dbg(dbg_ctl_cache_init, "http_ram_cache_size = %" PRId64 " = %" PRId64 "Mb", http_ram_cache_size,
            http_ram_cache_size / (1024 * 1024));
        int64_t stream_ram_cache_size = cache_config_ram_cache_size - http_ram_cache_size;

        Dbg(dbg_ctl_cache_init, "stream_ram_cache_size = %" PRId64 " = %" PRId64 "Mb", stream_ram_cache_size,
            stream_ram_cache_size / (1024 * 1024));

        // Dump some ram_cache size information in debug mode.
        Dbg(dbg_ctl_ram_cache, "config: size = %" PRId64 ", cutoff = %" PRId64 "", cache_config_ram_cache_size,
            cache_config_ram_cache_cutoff);
      }

      uint64_t total_cache_bytes     = 0; // bytes that can used in total_size
      uint64_t total_direntries      = 0; // all the direntries in the cache
      uint64_t used_direntries       = 0; //   and used
      uint64_t total_ram_cache_bytes = 0;

      for (int i = 0; i < gnstripes; i++) {
        StripeSM *stripe          = gstripes[i];
        int64_t   ram_cache_bytes = 0;

        if (stripe->cache_vol->ramcache_enabled) {
          if (http_ram_cache_size == 0) {
            // AUTO_SIZE_RAM_CACHE
            ram_cache_bytes = stripe->dirlen() * DEFAULT_RAM_CACHE_MULTIPLIER;
          } else {
            ink_assert(stripe->cache != nullptr);

            double factor = static_cast<double>(static_cast<int64_t>(stripe->len >> STORE_BLOCK_SHIFT)) / theCache->cache_size;
            Dbg(dbg_ctl_cache_init, "factor = %f", factor);

            ram_cache_bytes = static_cast<int64_t>(http_ram_cache_size * factor);
          }

          stripe->ram_cache->init(ram_cache_bytes, stripe);
          total_ram_cache_bytes += ram_cache_bytes;
          ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.ram_cache_bytes_total, ram_cache_bytes);

          Dbg(dbg_ctl_cache_init, "CacheProcessor::cacheInitialized[%d] - ram_cache_bytes = %" PRId64 " = %" PRId64 "Mb", i,
              ram_cache_bytes, ram_cache_bytes / (1024 * 1024));
        }

        uint64_t vol_total_cache_bytes  = stripe->len - stripe->dirlen();
        total_cache_bytes              += vol_total_cache_bytes;
        ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.bytes_total, vol_total_cache_bytes);
        ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.stripes);

        Dbg(dbg_ctl_cache_init, "total_cache_bytes = %" PRId64 " = %" PRId64 "Mb", total_cache_bytes,
            total_cache_bytes / (1024 * 1024));

        uint64_t vol_total_direntries  = stripe->directory.entries();
        total_direntries              += vol_total_direntries;
        ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.direntries_total, vol_total_direntries);

        uint64_t vol_used_direntries = dir_entries_used(stripe);
        ts::Metrics::Gauge::increment(stripe->cache_vol->vol_rsb.direntries_used, vol_used_direntries);
        used_direntries += vol_used_direntries;
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

      ts::Metrics::Gauge::store(cache_rsb.ram_cache_bytes_total, total_ram_cache_bytes);
      ts::Metrics::Gauge::store(cache_rsb.bytes_total, total_cache_bytes);
      ts::Metrics::Gauge::store(cache_rsb.direntries_total, total_direntries);
      ts::Metrics::Gauge::store(cache_rsb.direntries_used, used_direntries);

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
    CacheProcessor::initialized = CacheInitState::INITIALIZED;
    CacheProcessor::cache_ready = caches_ready;
    Note("cache enabled");
  } else {
    CacheProcessor::initialized = CacheInitState::FAILED;
    Note("cache disabled");
  }

  // Fire callback to signal initialization finished.
  if (cb_after_init) {
    cb_after_init();
  }

  // TS-3848
  if (CacheInitState::FAILED == CacheProcessor::initialized && cacheProcessor.waitForCache() > 1) {
    Emergency("Cache initialization failed with cache required, exiting.");
  }
}
