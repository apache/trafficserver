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

/****************************************************************************

  MultiCache.cc


 ****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/I_Layout.h"
#include "P_HostDB.h"
#include "P_MultiCache.h"
#include "P_EventSystem.h" // FIXME: need to have this in I_* header files.
#include "ts/ink_file.h"

static const int MC_SYNC_MIN_PAUSE_TIME = HRTIME_MSECONDS(200); // Pause for at least 200ms

MultiCacheBase::MultiCacheBase()
  : store(0), mapped_header(NULL), data(0), lowest_level_data(0), miss_stat(0), buckets_per_partitionF8(0)
{
  filename[0] = 0;
  memset(hit_stat, 0, sizeof(hit_stat));
  memset(unsunk, 0, sizeof(unsunk));
  for (int i     = 0; i < MULTI_CACHE_PARTITIONS; i++)
    unsunk[i].mc = this;
}

inline int
store_verify(Store *store)
{
  if (!store)
    return 0;
  for (unsigned i = 0; i < store->n_disks; i++) {
    for (Span *sd = store->disk[i]; sd; sd = sd->link.next) {
      if (!sd->file_pathname && sd->offset)
        return 0;
    }
  }
  return 1;
}

MultiCacheHeader::MultiCacheHeader()
  : magic(MULTI_CACHE_MAGIC_NUMBER),
    levels(0),
    tag_bits(0),
    max_hits(0),
    elementsize(0),
    buckets(0),
    totalelements(0),
    totalsize(0),
    nominal_elements(0),
    heap_size(0),
    heap_halfspace(0)
{
  memset(level_offset, 0, sizeof(level_offset));
  memset(bucketsize, 0, sizeof(bucketsize));
  memset(elements, 0, sizeof(elements));
  heap_used[0]      = 8;
  heap_used[1]      = 8;
  version.ink_major = MULTI_CACHE_MAJOR_VERSION;
  version.ink_minor = MULTI_CACHE_MINOR_VERSION;
}

static inline int
bytes_to_blocks(int64_t b)
{
  return (int)((b + (STORE_BLOCK_SIZE - 1)) / STORE_BLOCK_SIZE);
}

inline int
MultiCacheBase::blocks_in_level(unsigned int level)
{
  int64_t sumbytes = 0;
  int prevblocks   = 0;
  int b            = 0;
  for (unsigned int i = 0; i <= level; i++) {
    sumbytes += buckets * ((int64_t)bucketsize[i]);
    int sumblocks = bytes_to_blocks(sumbytes);
    b             = sumblocks - prevblocks;
    prevblocks    = sumblocks;
  }
  return b;
}

//
// Initialize MultiCache
// The outermost level of the cache contains ~aelements.
// The higher levels (lower in number) contain fewer.
//
int
MultiCacheBase::initialize(Store *astore, char *afilename, int aelements, int abuckets, unsigned int alevels,
                           int level0_elements_per_bucket, int level1_elements_per_bucket, int level2_elements_per_bucket)
{
  int64_t size = 0;

  Debug("multicache", "initializing %s with %d elements, %d buckets and %d levels", afilename, aelements, abuckets, alevels);
  ink_assert(alevels <= MULTI_CACHE_MAX_LEVELS);
  if (alevels > MULTI_CACHE_MAX_LEVELS) {
    Warning("Alevels too large %d, cannot initialize MultiCache", MULTI_CACHE_MAX_LEVELS);
    return -1;
  }
  levels           = alevels;
  elementsize      = get_elementsize();
  totalelements    = 0;
  nominal_elements = aelements;
  buckets          = abuckets;

  ink_strlcpy(filename, afilename, sizeof(filename));
  //
  //  Allocate level 2 as the outermost
  //
  if (levels > 2) {
    if (!buckets) {
      buckets = aelements / level2_elements_per_bucket;
      if (buckets < MULTI_CACHE_PARTITIONS)
        buckets = MULTI_CACHE_PARTITIONS;
    }
    if (levels == 3)
      level2_elements_per_bucket = aelements / buckets;
    elements[2]                  = level2_elements_per_bucket;
    totalelements += buckets * level2_elements_per_bucket;
    bucketsize[2] = elementsize * level2_elements_per_bucket;
    size += (int64_t)bucketsize[2] * (int64_t)buckets;

    if (!(level2_elements_per_bucket / level1_elements_per_bucket)) {
      Warning("Size change too large, unable to reconfigure");
      return -1;
    }

    aelements /= (level2_elements_per_bucket / level1_elements_per_bucket);
  }
  //
  //  Allocate level 1
  //
  if (levels > 1) {
    if (!buckets) {
      buckets = aelements / level1_elements_per_bucket;
      if (buckets < MULTI_CACHE_PARTITIONS)
        buckets = MULTI_CACHE_PARTITIONS;
    }
    if (levels == 2)
      level1_elements_per_bucket = aelements / buckets;
    elements[1]                  = level1_elements_per_bucket;
    totalelements += buckets * level1_elements_per_bucket;
    bucketsize[1] = elementsize * level1_elements_per_bucket;
    size += (int64_t)bucketsize[1] * (int64_t)buckets;
    if (!(level1_elements_per_bucket / level0_elements_per_bucket)) {
      Warning("Size change too large, unable to reconfigure");
      return -2;
    }
    aelements /= (level1_elements_per_bucket / level0_elements_per_bucket);
  }
  //
  //  Allocate level 0
  //
  if (!buckets) {
    buckets = aelements / level0_elements_per_bucket;
    if (buckets < MULTI_CACHE_PARTITIONS)
      buckets = MULTI_CACHE_PARTITIONS;
  }
  if (levels == 1)
    level0_elements_per_bucket = aelements / buckets;
  elements[0]                  = level0_elements_per_bucket;
  totalelements += buckets * level0_elements_per_bucket;
  bucketsize[0] = elementsize * level0_elements_per_bucket;
  size += (int64_t)bucketsize[0] * (int64_t)buckets;

  buckets_per_partitionF8 = (buckets << 8) / MULTI_CACHE_PARTITIONS;
  ink_release_assert(buckets_per_partitionF8);

  unsigned int blocks = (size + (STORE_BLOCK_SIZE - 1)) / STORE_BLOCK_SIZE;

  heap_size = int((float)totalelements * estimated_heap_bytes_per_entry());
  blocks += bytes_to_blocks(heap_size);

  blocks += 1; // header
  totalsize = (int64_t)blocks * (int64_t)STORE_BLOCK_SIZE;

  Debug("multicache", "heap_size = %d, totalelements = %d, totalsize = %d", heap_size, totalelements, totalsize);

  //
  //  Spread alloc from the store (using storage that can be mmapped)
  //
  delete store;
  store = new Store;
  astore->spread_alloc(*store, blocks, true);
  unsigned int got = store->total_blocks();

  if (got < blocks) {
    astore->free(*store);
    delete store;
    store = NULL;
    Warning("Configured store too small (actual=%d required=%d), unable to reconfigure", got * STORE_BLOCK_SIZE,
            blocks * STORE_BLOCK_SIZE);
    return -3;
  }
  totalsize = (STORE_BLOCK_SIZE)*blocks;

  level_offset[1] = buckets * bucketsize[0];
  level_offset[2] = buckets * bucketsize[1] + level_offset[1];

  if (lowest_level_data)
    delete[] lowest_level_data;
  lowest_level_data = new char[lowest_level_data_size()];
  ink_assert(lowest_level_data);
  memset(lowest_level_data, 0xFF, lowest_level_data_size());

  return got;
}

char *
MultiCacheBase::mmap_region(int blocks, int *fds, char *cur, size_t &total_length, bool private_flag, int zero_fill)
{
  if (!blocks)
    return cur;
  int p     = 0;
  char *res = 0;
  for (unsigned i = 0; i < store->n_disks; i++) {
    unsigned int target    = blocks / (store->n_disks - i);
    unsigned int following = store->total_blocks(i + 1);
    if (blocks - target > following)
      target = blocks - following;
    Span *ds = store->disk[i];
    for (unsigned j = 0; j < store->disk[i]->paths(); j++) {
      Span *d = ds->nth(j);

      ink_assert(d->is_mmapable());

      if (target && d->blocks) {
        int b = d->blocks;
        if (d->blocks > target)
          b = target;
        d->blocks -= b;
        unsigned int nbytes = b * STORE_BLOCK_SIZE;
        int fd              = fds[p] ? fds[p] : zero_fill;
        ink_assert(-1 != fd);
        int flags = private_flag ? MAP_PRIVATE : MAP_SHARED_MAP_NORESERVE;

        if (cur)
          res = (char *)mmap(cur, nbytes, PROT_READ | PROT_WRITE, MAP_FIXED | flags, fd, d->offset * STORE_BLOCK_SIZE);
        else
          res = (char *)mmap(cur, nbytes, PROT_READ | PROT_WRITE, flags, fd, d->offset * STORE_BLOCK_SIZE);

        d->offset += b;

        if (res == NULL || res == (caddr_t)MAP_FAILED)
          return NULL;
        ink_assert(!cur || res == cur);
        cur = res + nbytes;
        blocks -= b;
        total_length += nbytes; // total amount mapped.
      }
      p++;
    }
  }
  return blocks ? 0 : cur;
}

void
MultiCacheBase::reset()
{
  if (store)
    delete store;
  store = 0;
  if (lowest_level_data)
    delete[] lowest_level_data;
  lowest_level_data = 0;
  if (data)
    unmap_data();
  data = 0;
}

int
MultiCacheBase::unmap_data()
{
  int res = 0;
  if (data) {
    res  = munmap(data, totalsize);
    data = NULL;
    return res;
  }
  return 0;
}

int
MultiCacheBase::mmap_data(bool private_flag, bool zero_fill)
{
  ats_scoped_fd fd;
  int fds[MULTI_CACHE_MAX_FILES] = {0};
  int n_fds                      = 0;
  size_t total_mapped            = 0; // total mapped memory from storage.

  // open files
  //
  if (!store || !store->n_disks)
    goto Lalloc;
  for (unsigned i = 0; i < store->n_disks; i++) {
    Span *ds = store->disk[i];
    for (unsigned j = 0; j < store->disk[i]->paths(); j++) {
      char path[PATH_NAME_MAX];
      Span *d = ds->nth(j);
      int r   = d->path(filename, NULL, path, PATH_NAME_MAX);
      if (r < 0) {
        Warning("filename too large '%s'", filename);
        goto Labort;
      }
      fds[n_fds] = socketManager.open(path, O_RDWR | O_CREAT, 0644);
      if (fds[n_fds] < 0) {
        if (!zero_fill) {
          Warning("unable to open file '%s': %d, %s", path, errno, strerror(errno));
          goto Lalloc;
        }
        fds[n_fds] = 0;
      }
      if (!d->file_pathname) {
        struct stat fd_stat;

        if (fstat(fds[n_fds], &fd_stat) < 0) {
          Warning("unable to stat file '%s'", path);
          goto Lalloc;
        } else {
          int64_t size = (off_t)(d->blocks * STORE_BLOCK_SIZE);

          if (fd_stat.st_size != size) {
            int err = ink_file_fd_zerofill(fds[n_fds], size);

            if (err != 0) {
              Warning("unable to set file '%s' size to %" PRId64 ": %d, %s", path, size, err, strerror(err));
              goto Lalloc;
            }
          }
        }
      }
      n_fds++;
    }
  }

  data = 0;

  // mmap levels
  //
  {
    // make a copy of the store
    Store tStore;
    store->dup(tStore);
    Store *saved = store;
    store        = &tStore;

    char *cur = 0;

// find a good address to start
#if !defined(darwin)
    fd = socketManager.open("/dev/zero", O_RDONLY, 0645);
    if (fd < 0) {
      store = saved;
      Warning("unable to open /dev/zero: %d, %s", errno, strerror(errno));
      goto Labort;
    }
#endif

// lots of useless stuff
#if defined(darwin)
    cur = (char *)mmap(0, totalsize, PROT_READ, MAP_SHARED_MAP_NORESERVE | MAP_ANON, -1, 0);
#else
    cur = (char *)mmap(0, totalsize, PROT_READ, MAP_SHARED_MAP_NORESERVE, fd, 0);
#endif
    if (cur == NULL || cur == (caddr_t)MAP_FAILED) {
      store = saved;
#if defined(darwin)
      Warning("unable to mmap anonymous region for %u bytes: %d, %s", totalsize, errno, strerror(errno));
#else
      Warning("unable to mmap /dev/zero for %u bytes: %d, %s", totalsize, errno, strerror(errno));
#endif
      goto Labort;
    }
    if (munmap(cur, totalsize)) {
      store = saved;
#if defined(darwin)
      Warning("unable to munmap anonymous region for %u bytes: %d, %s", totalsize, errno, strerror(errno));
#else
      Warning("unable to munmap /dev/zero for %u bytes: %d, %s", totalsize, errno, strerror(errno));
#endif
      goto Labort;
    }

    /* We've done a mmap on a target region of the maximize size we need. Now we drop that mapping
       and do the real one, keeping at the same address space (stored in @a data) which should work because
       we just tested it.
    */
    // coverity[use_after_free]
    data = cur;

    cur = mmap_region(blocks_in_level(0), fds, cur, total_mapped, private_flag, fd);
    if (!cur) {
      store = saved;
      goto Labort;
    }
    if (levels > 1)
      cur = mmap_region(blocks_in_level(1), fds, cur, total_mapped, private_flag, fd);
    if (!cur) {
      store = saved;
      goto Labort;
    }
    if (levels > 2)
      cur = mmap_region(blocks_in_level(2), fds, cur, total_mapped, private_flag, fd);
    if (!cur) {
      store = saved;
      goto Labort;
    }

    if (heap_size) {
      heap = cur;
      cur  = mmap_region(bytes_to_blocks(heap_size), fds, cur, total_mapped, private_flag, fd);
      if (!cur) {
        store = saved;
        goto Labort;
      }
    }
    mapped_header = (MultiCacheHeader *)cur;
    if (!mmap_region(1, fds, cur, total_mapped, private_flag, fd)) {
      store = saved;
      goto Labort;
    }
#if !defined(darwin)
    ink_assert(!socketManager.close(fd));
#endif
    store = saved;
  }

  for (int i = 0; i < n_fds; i++) {
    if (fds[i] >= 0)
      ink_assert(!socketManager.close(fds[i]));
  }

  return 0;
Lalloc : {
  free(data);
  char *cur = 0;

  data = (char *)ats_memalign(ats_pagesize(), totalsize);
  cur  = data + STORE_BLOCK_SIZE * blocks_in_level(0);
  if (levels > 1)
    cur = data + STORE_BLOCK_SIZE * blocks_in_level(1);
  if (levels > 2)
    cur = data + STORE_BLOCK_SIZE * blocks_in_level(2);
  if (heap_size) {
    heap = cur;
    cur += bytes_to_blocks(heap_size) * STORE_BLOCK_SIZE;
  }
  mapped_header = (MultiCacheHeader *)cur;
  for (int i = 0; i < n_fds; i++) {
    if (fds[i] >= 0)
      socketManager.close(fds[i]);
  }

  return 0;
}

Labort:
  for (int i = 0; i < n_fds; i++) {
    if (fds[i] >= 0)
      socketManager.close(fds[i]);
  }
  if (total_mapped > 0)
    munmap(data, total_mapped);

  return -1;
}

void
MultiCacheBase::clear()
{
  memset(data, 0, totalsize);
  heap_used[0]   = 8;
  heap_used[1]   = 8;
  heap_halfspace = 0;
  *mapped_header = *(MultiCacheHeader *)this;
}

void
MultiCacheBase::clear_but_heap()
{
  memset(data, 0, totalelements * elementsize);
  *mapped_header = *(MultiCacheHeader *)this;
}

int
MultiCacheBase::read_config(const char *config_filename, Store &s, char *fn, int *pi, int *pbuck)
{
  int scratch;
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  char p[PATH_NAME_MAX], buf[256];

  Layout::relative_to(p, sizeof(p), rundir, config_filename);

  ats_scoped_fd fd(::open(p, O_RDONLY));
  if (fd < 0)
    return 0;

  if (ink_file_fd_readline(fd, sizeof(buf), buf) <= 0)
    return -1;
  // coverity[secure_coding]
  if (sscanf(buf, "%d\n", pi ? pi : &scratch) != 1)
    return -1;

  if (ink_file_fd_readline(fd, sizeof(buf), buf) <= 0)
    return -1;
  // coverity[secure_coding]
  if (sscanf(buf, "%d\n", pbuck ? pbuck : &scratch) != 1)
    return -1;

  if (ink_file_fd_readline(fd, sizeof(buf), buf) <= 0)
    return -1;
  // coverity[secure_coding]
  if (sscanf(buf, "%d\n", &heap_size) != 1)
    return -1;

  if (s.read(fd, fn) < 0)
    return -1;

  return 1;
}

int
MultiCacheBase::write_config(const char *config_filename, int nominal_size, int abuckets)
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  char p[PATH_NAME_MAX], buf[256];
  int fd, retcode = -1;

  Layout::relative_to(p, sizeof(p), rundir, config_filename);

  if ((fd = ::open(p, O_CREAT | O_WRONLY | O_TRUNC, 0644)) >= 0) {
    snprintf(buf, sizeof(buf) - 1, "%d\n%d\n%d\n", nominal_size, abuckets, heap_size);
    buf[sizeof(buf) - 1] = 0;
    if (ink_file_fd_writestring(fd, buf) != -1 && store->write(fd, filename) >= 0)
      retcode = 0;
    ::close(fd);
  } else
    Warning("unable to open '%s' for write: %d, %s", p, errno, strerror(errno));

  return retcode;
}

int
MultiCacheBase::open(Store *s, const char *config_filename, char *db_filename, int db_size, bool reconfigure, bool fix, bool silent)
{
  int ret         = 0;
  const char *err = NULL;
  char *serr      = NULL;
  char t_db_filename[PATH_NAME_MAX];
  int t_db_size    = 0;
  int t_db_buckets = 0;
  int change       = 0;

  t_db_filename[0] = 0;

  // Set up cache
  {
    Store tStore;
    int res = read_config(config_filename, tStore, t_db_filename, &t_db_size, &t_db_buckets);

    ink_assert(store_verify(&tStore));
    if (res < 0)
      goto LfailRead;
    if (!res) {
      if (!reconfigure || !db_filename || !db_size)
        goto LfailConfig;
      if (initialize(s, db_filename, db_size) <= 0)
        goto LfailInit;
      write_config(config_filename, db_size, buckets);
      if (mmap_data() < 0)
        goto LfailMap;
      clear();
    } else {
      // don't know how to rebuild from this problem
      ink_assert(!db_filename || !strcmp(t_db_filename, db_filename));
      if (!db_filename)
        db_filename = t_db_filename;

      // Has the size changed?
      change = (db_size >= 0) ? (db_size - t_db_size) : 0;
      if (db_size < 0)
        db_size = t_db_size;
      if (change && !reconfigure)
        goto LfailConfig;

      Store cStore;
      tStore.dup(cStore);

      // Try to get back our storage
      Store diff;

      s->try_realloc(cStore, diff);
      if (diff.n_disks && !reconfigure)
        goto LfailConfig;

      // Do we need to do a reconfigure?
      if (diff.n_disks || change) {
        // find a new store to old the amount of space we need
        int delta = change;

        if (diff.n_disks)
          delta += diff.total_blocks();

        if (delta) {
          if (delta > 0) {
            Store freeStore;
            stealStore(freeStore, delta);
            Store more;
            freeStore.spread_alloc(more, delta);
            if (delta > (int)more.total_blocks())
              goto LfailReconfig;
            Store more_diff;
            s->try_realloc(more, more_diff);
            if (more_diff.n_disks)
              goto LfailReconfig;
            cStore.add(more);
            if (more.clear(db_filename, false) < 0)
              goto LfailReconfig;
          }
          if (delta < 0) {
            Store removed;
            cStore.spread_alloc(removed, -delta);
          }
        }
        cStore.sort();
        if (initialize(&cStore, db_filename, db_size, t_db_buckets) <= 0)
          goto LfailInit;

        ink_assert(store_verify(store));

        if (write_config(config_filename, db_size, buckets) < 0)
          goto LfailWrite;

        ink_assert(store_verify(store));

        //  rebuild
        MultiCacheBase *old = dup();
        if (old->initialize(&tStore, t_db_filename, t_db_size, t_db_buckets) <= 0) {
          delete old;
          goto LfailInit;
        }

        if (rebuild(*old)) {
          delete old;
          goto LfailRebuild;
        }
        ink_assert(store_verify(store));
        delete old;

      } else {
        if (initialize(&tStore, db_filename, db_size, t_db_buckets) <= 0)
          goto LfailFix;
        ink_assert(store_verify(store));
        if (mmap_data() < 0)
          goto LfailMap;
        if (!verify_header())
          goto LheaderCorrupt;
        *(MultiCacheHeader *)this = *mapped_header;
        ink_assert(store_verify(store));

        if (fix)
          if (check(config_filename, true) < 0)
            goto LfailFix;
      }
    }
  }

  if (store)
    ink_assert(store_verify(store));
Lcontinue:
  return ret;

LheaderCorrupt:
  err = "header missing/corrupt";
  goto Lfail;

LfailWrite:
  err  = "unable to write";
  serr = strerror(errno);
  goto Lfail;

LfailRead:
  err  = "unable to read";
  serr = strerror(errno);
  goto Lfail;

LfailInit:
  err = "unable to initialize database (too little storage)\n";
  goto Lfail;

LfailConfig:
  err = "configuration changed";
  goto Lfail;

LfailReconfig:
  err = "unable to reconfigure";
  goto Lfail;

LfailRebuild:
  err = "unable to rebuild";
  goto Lfail;

LfailFix:
  err = "unable to fix";
  goto Lfail;

LfailMap:
  err  = "unable to mmap";
  serr = strerror(errno);
  goto Lfail;

Lfail : {
  unmap_data();
  if (!silent) {
    if (reconfigure) {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s: [%s] %s: disabling database\n"
                                                "You may need to 'reconfigure' your cache manually.  Please refer to\n"
                                                "the 'Configuration' chapter in the manual.",
                       err, config_filename, serr ? serr : "");
    } else {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s: [%s] %s: reinitializing database", err, config_filename, serr ? serr : "");
    }
  }
}
  ret = -1;
  goto Lcontinue;
}

bool
MultiCacheBase::verify_header()
{
  return mapped_header->magic == magic && mapped_header->version.ink_major == version.ink_major &&
         mapped_header->version.ink_minor == version.ink_minor && mapped_header->levels == levels &&
         mapped_header->tag_bits == tag_bits && mapped_header->max_hits == max_hits && mapped_header->elementsize == elementsize &&
         mapped_header->buckets == buckets && mapped_header->level_offset[0] == level_offset[0] &&
         mapped_header->level_offset[1] == level_offset[1] && mapped_header->level_offset[2] == level_offset[2] &&
         mapped_header->elements[0] == elements[0] && mapped_header->elements[1] == elements[1] &&
         mapped_header->elements[2] == elements[2] && mapped_header->bucketsize[0] == bucketsize[0] &&
         mapped_header->bucketsize[1] == bucketsize[1] && mapped_header->bucketsize[2] == bucketsize[2] &&
         mapped_header->totalelements == totalelements && mapped_header->totalsize == totalsize &&
         mapped_header->nominal_elements == nominal_elements;
}

void
MultiCacheBase::print_info(FILE *fp)
{ // STDIO OK
  fprintf(fp, "    Elements:       %-10d\n", totalelements);
  fprintf(fp, "    Size (bytes):   %-10u\n", totalsize);
}

//
//  We need to preserve the buckets
// while moving the existing data into the new locations.
//
// if data == NULL we are rebuilding (as opposed to check or fix)
//
int
MultiCacheBase::rebuild(MultiCacheBase &old, int kind)
{
  char *new_data = 0;

  ink_assert(store_verify(store));
  ink_assert(store_verify(old.store));

  // map in a chunk of space to use as scratch (check)
  // or to copy the database to.
  ats_scoped_fd fd(socketManager.open("/dev/zero", O_RDONLY));
  if (fd < 0) {
    Warning("unable to open /dev/zero: %d, %s", errno, strerror(errno));
    return -1;
  }

  new_data = (char *)mmap(0, old.totalsize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

  ink_assert(data != new_data);
  if (new_data == NULL || new_data == (caddr_t)MAP_FAILED) {
    Warning("unable to mmap /dev/zero for %u bytes: %d, %s", totalsize, errno, strerror(errno));
    return -1;
  }
  // if we are rebuilding get the original data

  if (!data) {
    ink_assert(kind == MC_REBUILD);
    if (old.mmap_data(true, true) < 0)
      return -1;
    memcpy(new_data, old.data, old.totalsize);
    old.unmap_data();
    // now map the new location
    if (mmap_data() < 0)
      return -1;
    // old.data is the copy
    old.data = new_data;
  } else {
    ink_assert(kind == MC_REBUILD_CHECK || kind == MC_REBUILD_FIX);
    if (kind == MC_REBUILD_CHECK) {
      // old.data is the original, data is the copy
      old.data = data;
      data     = new_data;
    } else {
      memcpy(new_data, data, old.totalsize);
      // old.data is the copy, data is the original
      old.data = new_data;
    }
  }

  ink_assert(buckets == old.buckets);

  FILE *diag_output_fp = stderr;

  RebuildMC r;

  r.data = old.data;

  r.rebuild = kind == MC_REBUILD;
  r.check   = kind == MC_REBUILD_CHECK;
  r.fix     = kind == MC_REBUILD_FIX;

  r.deleted    = 0;
  r.backed     = 0;
  r.duplicates = 0;
  r.stale      = 0;
  r.corrupt    = 0;
  r.good       = 0;
  r.total      = 0;

  if (r.rebuild)
    fprintf(diag_output_fp, "New:\n");
  print_info(diag_output_fp);
  if (r.rebuild || r.fix) {
    fprintf(diag_output_fp, "Old:\n");
    old.print_info(diag_output_fp);
    clear_but_heap();
  }

  fprintf(diag_output_fp, "    [processing element.. ");

  int scan = 0;
  for (int l = old.levels - 1; l >= 0; l--)
    for (int b = 0; b < old.buckets; b++) {
      r.partition = partition_of_bucket(b);
      for (int e = 0; e < old.elements[l]; e++) {
        scan++;
        if (!(scan & 0x7FFF))
          fprintf(diag_output_fp, "%d ", scan);
        char *x = old.data + old.level_offset[l] + b * old.bucketsize[l] + e * elementsize;
        rebuild_element(b, x, r);
      }
    }
  if (scan & 0x7FFF)
    printf("done]\n");
  if (r.rebuild || r.fix)
    for (int p = 0; p < MULTI_CACHE_PARTITIONS; p++)
      sync_partition(p);

  fprintf(diag_output_fp, "    Usage Summary\n");
  fprintf(diag_output_fp, "\tTotal:      %-10d\n", r.total);
  if (r.good)
    fprintf(diag_output_fp, "\tGood:       %.2f%% (%d)\n", r.total ? ((r.good * 100.0) / r.total) : 0, r.good);
  if (r.deleted)
    fprintf(diag_output_fp, "\tDeleted:    %5.2f%% (%d)\n", r.deleted ? ((r.deleted * 100.0) / r.total) : 0.0, r.deleted);
  if (r.backed)
    fprintf(diag_output_fp, "\tBacked:     %5.2f%% (%d)\n", r.backed ? ((r.backed * 100.0) / r.total) : 0.0, r.backed);
  if (r.duplicates)
    fprintf(diag_output_fp, "\tDuplicates: %5.2f%% (%d)\n", r.duplicates ? ((r.duplicates * 100.0) / r.total) : 0.0, r.duplicates);
  if (r.stale)
    fprintf(diag_output_fp, "\tStale:      %5.2f%% (%d)\n", r.stale ? ((r.stale * 100.0) / r.total) : 0.0, r.stale);
  if (r.corrupt)
    fprintf(diag_output_fp, "\tCorrupt:    %5.2f%% (%d)\n", r.corrupt ? ((r.corrupt * 100.0) / r.total) : 0.0, r.corrupt);

  old.reset();

  return 0;
}

int
MultiCacheBase::check(const char *config_filename, bool fix)
{
  //  rebuild
  Store tStore;
  char t_db_filename[PATH_NAME_MAX];
  t_db_filename[0] = 0;
  int t_db_size = 0, t_db_buckets = 0;
  if (read_config(config_filename, tStore, t_db_filename, &t_db_size, &t_db_buckets) <= 0)
    return -1;

  MultiCacheBase *old = dup();

  if (old->initialize(&tStore, filename, nominal_elements, buckets) <= 0) {
    delete old;
    return -1;
  }

  int res = rebuild(*old, fix ? MC_REBUILD_FIX : MC_REBUILD_CHECK);
  delete old;
  return res;
}

int
MultiCacheBase::sync_heap(int part)
{
  if (heap_size) {
    int b_per_part = heap_size / MULTI_CACHE_PARTITIONS;
    if (ats_msync(data + level_offset[2] + buckets * bucketsize[2] + b_per_part * part, b_per_part, data + totalsize, MS_SYNC) < 0)
      return -1;
  }
  return 0;
}

//
// Sync a single partition
//
// Since we delete from the higher levels
// and insert into the lower levels,
// start with the higher levels to reduce the risk of duplicates.
//
int
MultiCacheBase::sync_partition(int partition)
{
  int res = 0;
  int b   = first_bucket_of_partition(partition);
  int n   = buckets_of_partition(partition);
  // L3
  if (levels > 2) {
    if (ats_msync(data + level_offset[2] + b * bucketsize[2], n * bucketsize[2], data + totalsize, MS_SYNC) < 0)
      res = -1;
  }
  // L2
  if (levels > 1) {
    if (ats_msync(data + level_offset[1] + b * bucketsize[1], n * bucketsize[1], data + totalsize, MS_SYNC) < 0)
      res = -1;
  }
  // L1
  if (ats_msync(data + b * bucketsize[0], n * bucketsize[0], data + totalsize, MS_SYNC) < 0)
    res = -1;
  return res;
}

int
MultiCacheBase::sync_header()
{
  *mapped_header = *(MultiCacheHeader *)this;
  return ats_msync((char *)mapped_header, STORE_BLOCK_SIZE, (char *)mapped_header + STORE_BLOCK_SIZE, MS_SYNC);
}

int
MultiCacheBase::sync_all()
{
  int res = 0, i = 0;
  for (i = 0; i < MULTI_CACHE_PARTITIONS; i++)
    if (sync_heap(i) < 0)
      res = -1;
  for (i = 0; i < MULTI_CACHE_PARTITIONS; i++)
    if (sync_partition(i) < 0)
      res = -1;
  if (sync_header())
    res = -1;
  return res;
}

//
// Syncs MulitCache
//
struct MultiCacheSync;
typedef int (MultiCacheSync::*MCacheSyncHandler)(int, void *);

struct MultiCacheSync : public Continuation {
  int partition;
  MultiCacheBase *mc;
  Continuation *cont;
  int before_used;

  int
  heapEvent(int event, Event *e)
  {
    if (!partition) {
      before_used     = mc->heap_used[mc->heap_halfspace];
      mc->header_snap = *(MultiCacheHeader *)mc;
    }
    if (partition < MULTI_CACHE_PARTITIONS) {
      mc->sync_heap(partition++);
      e->schedule_imm();
      return EVENT_CONT;
    }
    *mc->mapped_header = mc->header_snap;
    ink_assert(!ats_msync((char *)mc->mapped_header, STORE_BLOCK_SIZE, (char *)mc->mapped_header + STORE_BLOCK_SIZE, MS_SYNC));
    partition = 0;
    SET_HANDLER((MCacheSyncHandler)&MultiCacheSync::mcEvent);
    return mcEvent(event, e);
  }

  int
  mcEvent(int event, Event *e)
  {
    (void)event;
    if (partition >= MULTI_CACHE_PARTITIONS) {
      cont->handleEvent(MULTI_CACHE_EVENT_SYNC, 0);
      Debug("multicache", "MultiCacheSync done (%d, %d)", mc->heap_used[0], mc->heap_used[1]);
      delete this;
      return EVENT_DONE;
    }
    mc->fixup_heap_offsets(partition, before_used);
    mc->sync_partition(partition);
    partition++;
    mutex = e->ethread->mutex;
    SET_HANDLER((MCacheSyncHandler)&MultiCacheSync::pauseEvent);
    e->schedule_in(MAX(MC_SYNC_MIN_PAUSE_TIME, HRTIME_SECONDS(hostdb_sync_frequency - 5) / MULTI_CACHE_PARTITIONS));
    return EVENT_CONT;
  }

  int
  pauseEvent(int event, Event *e)
  {
    (void)event;
    (void)e;
    if (partition < MULTI_CACHE_PARTITIONS)
      mutex = mc->locks[partition];
    else
      mutex = cont->mutex;
    SET_HANDLER((MCacheSyncHandler)&MultiCacheSync::mcEvent);
    e->schedule_imm();
    return EVENT_CONT;
  }

  MultiCacheSync(Continuation *acont, MultiCacheBase *amc)
    : Continuation(amc->locks[0]), partition(0), mc(amc), cont(acont), before_used(0)
  {
    mutex = mc->locks[partition];
    SET_HANDLER((MCacheSyncHandler)&MultiCacheSync::heapEvent);
  }
};

//
// Heap code
//

UnsunkPtrRegistry *
MultiCacheBase::fixup_heap_offsets(int partition, int before_used, UnsunkPtrRegistry *r, int base)
{
  if (!r)
    r        = &unsunk[partition];
  bool found = 0;
  for (int i = 0; i < r->n; i++) {
    UnsunkPtr &p = r->ptrs[i];
    if (p.offset) {
      Debug("multicache", "fixup p.offset %d offset %d %" PRId64 " part %d", p.offset, *p.poffset,
            (int64_t)((char *)p.poffset - data), partition);
      if (*p.poffset == -(i + base) - 1) {
        if (halfspace_of(p.offset) != heap_halfspace) {
          ink_assert(0);
          *p.poffset = 0;
        } else {
          if (p.offset < before_used) {
            *p.poffset = p.offset + 1;
            ink_assert(*p.poffset);
          } else
            continue;
        }
      } else {
        Debug("multicache", "not found %" PRId64 " i %d base %d *p.poffset = %d", (int64_t)((char *)p.poffset - data), i, base,
              *p.poffset);
      }
      p.offset     = 0;
      p.poffset    = (int *)r->next_free;
      r->next_free = &p;
      found        = true;
    }
  }
  if (r->next) {
    int s   = MULTI_CACHE_UNSUNK_PTR_BLOCK_SIZE(totalelements);
    r->next = fixup_heap_offsets(partition, before_used, r->next, base + s);
  }
  if (!r->next && !found && r != &unsunk[partition]) {
    delete r;
    return NULL;
  }
  return r;
}

struct OffsetTable {
  int new_offset;
  int *poffset;
};

struct MultiCacheHeapGC;
typedef int (MultiCacheHeapGC::*MCacheHeapGCHandler)(int, void *);
struct MultiCacheHeapGC : public Continuation {
  Continuation *cont;
  MultiCacheBase *mc;
  int partition;
  int n_offsets;
  OffsetTable *offset_table;

  int
  startEvent(int event, Event *e)
  {
    (void)event;
    if (partition < MULTI_CACHE_PARTITIONS) {
      // copy heap data

      char *before = mc->heap + mc->heap_used[mc->heap_halfspace];
      mc->copy_heap(partition, this);
      char *after = mc->heap + mc->heap_used[mc->heap_halfspace];

      // sync new heap data and header (used)

      if (after - before > 0) {
        ink_assert(!ats_msync(before, after - before, mc->heap + mc->totalsize, MS_SYNC));
        ink_assert(!ats_msync((char *)mc->mapped_header, STORE_BLOCK_SIZE, (char *)mc->mapped_header + STORE_BLOCK_SIZE, MS_SYNC));
      }
      // update table to point to new entries

      for (int i = 0; i < n_offsets; i++) {
        int *i1, i2;
        // BAD CODE GENERATION ON THE ALPHA
        //*(offset_table[i].poffset) = offset_table[i].new_offset + 1;
        i1  = offset_table[i].poffset;
        i2  = offset_table[i].new_offset + 1;
        *i1 = i2;
      }
      n_offsets = 0;
      mc->sync_partition(partition);
      partition++;
      if (partition < MULTI_CACHE_PARTITIONS)
        mutex = mc->locks[partition];
      else
        mutex = cont->mutex;
      e->schedule_in(MAX(MC_SYNC_MIN_PAUSE_TIME, HRTIME_SECONDS(hostdb_sync_frequency - 5) / MULTI_CACHE_PARTITIONS));
      return EVENT_CONT;
    }
    mc->heap_used[mc->heap_halfspace ? 0 : 1] = 8; // skip 0
    cont->handleEvent(MULTI_CACHE_EVENT_SYNC, 0);
    Debug("multicache", "MultiCacheHeapGC done");
    delete this;
    return EVENT_DONE;
  }

  MultiCacheHeapGC(Continuation *acont, MultiCacheBase *amc)
    : Continuation(amc->locks[0]), cont(acont), mc(amc), partition(0), n_offsets(0)
  {
    SET_HANDLER((MCacheHeapGCHandler)&MultiCacheHeapGC::startEvent);
    offset_table = (OffsetTable *)ats_malloc(sizeof(OffsetTable) *
                                             ((mc->totalelements / MULTI_CACHE_PARTITIONS) + mc->elements[mc->levels - 1] * 3 + 1));
    // flip halfspaces
    mutex              = mc->locks[partition];
    mc->heap_halfspace = mc->heap_halfspace ? 0 : 1;
  }
  ~MultiCacheHeapGC() { ats_free(offset_table); }
};

void
MultiCacheBase::sync_partitions(Continuation *cont)
{
  // don't try to sync if we were not correctly initialized
  if (data && mapped_header) {
    if (heap_used[heap_halfspace] > halfspace_size() * MULTI_CACHE_HEAP_HIGH_WATER)
      eventProcessor.schedule_imm(new MultiCacheHeapGC(cont, this), ET_TASK);
    else
      eventProcessor.schedule_imm(new MultiCacheSync(cont, this), ET_TASK);
  }
}

void
MultiCacheBase::copy_heap_data(char *src, int s, int *pi, int partition, MultiCacheHeapGC *gc)
{
  char *dest = (char *)alloc(NULL, s);
  Debug("multicache", "copy %p to %p", src, dest);
  if (dest) {
    memcpy(dest, src, s);
    if (*pi < 0) { // already in the unsunk ptr registry, ok to change there
      UnsunkPtr *ptr = unsunk[partition].ptr(-*pi - 1);
      if (ptr->poffset == pi)
        ptr->offset = dest - heap;
      else {
        ink_assert(0);
        *pi = 0;
      }
    } else {
      gc->offset_table[gc->n_offsets].new_offset = dest - heap;
      gc->offset_table[gc->n_offsets].poffset    = pi;
      gc->n_offsets++;
    }
  } else {
    ink_assert(0);
    *pi = 0;
  }
}

UnsunkPtrRegistry::UnsunkPtrRegistry() : mc(NULL), n(0), ptrs(NULL), next_free(NULL), next(NULL)
{
}

UnsunkPtrRegistry::~UnsunkPtrRegistry()
{
  ats_free(ptrs);
}

void
UnsunkPtrRegistry::alloc_data()
{
  int bs   = MULTI_CACHE_UNSUNK_PTR_BLOCK_SIZE(mc->totalelements);
  size_t s = bs * sizeof(UnsunkPtr);
  ptrs     = (UnsunkPtr *)ats_malloc(s);
  for (int i = 0; i < bs; i++) {
    ptrs[i].offset  = 0;
    ptrs[i].poffset = (int *)&ptrs[i + 1];
  }
  ptrs[bs - 1].poffset = NULL;
  next_free            = ptrs;
  n                    = bs;
}

UnsunkPtr *
UnsunkPtrRegistry::alloc(int *poffset, int base)
{
  if (next_free) {
    UnsunkPtr *res = next_free;
    next_free      = (UnsunkPtr *)next_free->poffset;
    *poffset       = -(base + (res - ptrs)) - 1;
    ink_assert(*poffset);
    return res;
  } else {
    if (!ptrs) {
      alloc_data();
      return alloc(poffset, base);
    }
    if (!next) {
      next     = new UnsunkPtrRegistry;
      next->mc = mc;
    }
    int s = MULTI_CACHE_UNSUNK_PTR_BLOCK_SIZE(mc->totalelements);
    return next->alloc(poffset, base + s);
  }
}

void *
MultiCacheBase::alloc(int *poffset, int asize)
{
  int h    = heap_halfspace;
  int size = (asize + MULTI_CACHE_HEAP_ALIGNMENT - 1) & ~(MULTI_CACHE_HEAP_ALIGNMENT - 1);
  int o    = ink_atomic_increment((int *)&heap_used[h], size);

  if (o + size > halfspace_size()) {
    ink_atomic_increment((int *)&heap_used[h], -size);
    ink_assert(!"out of space");
    if (poffset)
      *poffset = 0;
    return NULL;
  }
  int offset = (h ? halfspace_size() : 0) + o;
  char *p    = heap + offset;
  if (poffset) {
    int part = ptr_to_partition((char *)poffset);
    if (part < 0)
      return NULL;
    UnsunkPtr *up = unsunk[part].alloc(poffset);
    up->offset    = offset;
    up->poffset   = poffset;
    Debug("multicache", "alloc unsunk %d at %" PRId64 " part %d offset %d", *poffset, (int64_t)((char *)poffset - data), part,
          offset);
  }
  return (void *)p;
}

UnsunkPtr *
UnsunkPtrRegistry::ptr(int i)
{
  if (i >= n) {
    if (!next)
      return NULL;
    else
      return next->ptr(i - n);
  } else {
    if (!ptrs)
      return NULL;
    return &ptrs[i];
  }
}

void *
MultiCacheBase::ptr(int *poffset, int partition)
{
  int o = *poffset;
  Debug("multicache", "ptr %" PRId64 " part %d %d", (int64_t)((char *)poffset - data), partition, o);
  if (o > 0) {
    if (!valid_offset(o)) {
      ink_assert(!"bad offset");
      *poffset = 0;
      return NULL;
    }
    return (void *)(heap + o - 1);
  }
  if (!o)
    return NULL;
  UnsunkPtr *p = unsunk[partition].ptr(-o - 1);
  if (!p)
    return NULL;
  if (p->poffset != poffset)
    return NULL;
  return (void *)(heap + p->offset);
}

void
MultiCacheBase::update(int *poffset, int *old_poffset)
{
  int o = *poffset;
  Debug("multicache", "updating %" PRId64 " %d", (int64_t)((char *)poffset - data), o);
  if (o > 0) {
    if (!valid_offset(o)) {
      ink_assert(!"bad poffset");
      *poffset = 0;
    }
    return;
  }
  if (!o)
    return;

  int part = ptr_to_partition((char *)poffset);

  if (part < 0)
    return;

  UnsunkPtr *p = unsunk[part].ptr(-*old_poffset - 1);
  if (!p || p->poffset != old_poffset) {
    *poffset = 0;
    return;
  }
  ink_assert(p->poffset != poffset);
  UnsunkPtr *n = unsunk[part].alloc(poffset);
  n->poffset   = poffset;
  n->offset    = p->offset;
}

int
MultiCacheBase::ptr_to_partition(char *ptr)
{
  int o = ptr - data;
  if (o < level_offset[0])
    return -1;
  if (o < level_offset[1])
    return partition_of_bucket((o - level_offset[0]) / bucketsize[0]);
  if (o < level_offset[2])
    return partition_of_bucket((o - level_offset[1]) / bucketsize[1]);
  if (o < level_offset[2] + elements[2] * elementsize)
    return partition_of_bucket((o - level_offset[2]) / bucketsize[2]);
  return -1;
}

void
stealStore(Store &s, int blocks)
{
  if (s.read_config())
    return;
  Store tStore;
  MultiCacheBase dummy;
  if (dummy.read_config("hostdb.config", tStore) > 0) {
    Store dStore;
    s.try_realloc(tStore, dStore);
  }
  tStore.delete_all();
  if (dummy.read_config("dir.config", tStore) > 0) {
    Store dStore;
    s.try_realloc(tStore, dStore);
  }
  tStore.delete_all();
  if (dummy.read_config("alt.config", tStore) > 0) {
    Store dStore;
    s.try_realloc(tStore, dStore);
  }
  // grab some end portion of some block... so as not to damage the
  // pool header
  for (unsigned d = 0; d < s.n_disks;) {
    Span *ds = s.disk[d];
    while (ds) {
      if (!blocks)
        ds->blocks = 0;
      else {
        int b = blocks;
        if ((int)ds->blocks < blocks)
          b = ds->blocks;
        if (ds->file_pathname)
          ds->offset += (ds->blocks - b);
        ds->blocks = b;
        blocks -= b;
      }
      ds = ds->link.next;
    }
    d++;
  }
}
