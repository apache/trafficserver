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
#include <memory>
#include <shared_mutex>
#include "P_Cache.h"
#include "tscore/MatcherUtils.h"
#include "tscore/HostLookup.h"

#define CACHE_MEM_FREE_TIMEOUT HRTIME_SECONDS(1)

struct Vol;
struct CacheVol;

struct CacheHostResult;
struct Cache;

struct CacheHostRecord {
  int Init(CacheType typ);
  int Init(matcher_line *line_info, CacheType typ);

  void UpdateMatch(CacheHostResult *r);
  void Print() const;

  ~CacheHostRecord()
  {
    ats_free(vols);
    ats_free(vol_hash_table);
    ats_free(cp);
  }

  CacheType type                 = CACHE_NONE_TYPE;
  Vol **vols                     = nullptr;
  int good_num_vols              = 0;
  int num_vols                   = 0;
  int num_initialized            = 0;
  unsigned short *vol_hash_table = nullptr;
  CacheVol **cp                  = nullptr;
  int num_cachevols              = 0;

  CacheHostRecord() {}
};

void build_vol_hash_table(CacheHostRecord *cp);

struct CacheHostResult {
  CacheHostRecord *record = nullptr;

  CacheHostResult() {}
};

class CacheHostMatcher
{
public:
  CacheHostMatcher(const char *name, CacheType typ);
  ~CacheHostMatcher();

  void AllocateSpace(int num_entries);
  void NewEntry(matcher_line *line_info);

  void Match(const char *rdata, int rlen, CacheHostResult *result) const;
  void Print() const;

  int
  getNumElements() const
  {
    return num_el;
  }
  CacheHostRecord *
  getDataArray() const
  {
    return data_array;
  }
  HostLookup *
  getHLookup() const
  {
    return host_lookup;
  }

private:
  static void PrintFunc(void *opaque_data);
  HostLookup *host_lookup;     // Data structure to do the lookups
  CacheHostRecord *data_array; // array of all data items
  int array_len;               // the length of the arrays
  int num_el;                  // the number of items in the tree
  CacheType type;
};

// ReplaceablePtr provides threadsafe access to an object which may be replaced.
//
// Access is provided via ScopedReader and ScopedWriter classes, which acquire
// shared (read) and exclusive (write) locks respectively on construction and
// release them upon destruction.
//
// The underlying object may be replaced by a concurrent thread via 'reset',
// which acquires an exclusive lock before setting the internal pointer. This
// takes ownership of the given pointer. If this already has ownership of
// another pointer, that object is destroyed and its memory freed.
//
// Direct access without acquiring the lock is intentionally not provided, to
// prevent accidental usage without locking, or forgetting to release the lock.
//
// This may not be copied. To use, construct with one owner, and pass a pointer
// to other users.
//
template <typename T> class ReplaceablePtr
{
public:
  ReplaceablePtr() {}
  virtual ~ReplaceablePtr() {}

  // reset acquires an exclusive (write) lock and updates the internal pointer
  // with t.
  // If an existing pointer is owned, the object is destructed and its memory
  // freed.
  void
  reset(T *t)
  {
    std::scoped_lock l(m);
    h.reset(t);
  }

  // ScopedReader constructs an object which is allowed to read from a
  // ReplaceablePtr.
  //
  // The lifetime of the ReplaceablePtr must exceed the lifetime of this.
  // The shared (read) lock is immediately acquired  upon construction of this,
  // and released upon destruction.
  class ScopedReader
  {
  public:
    ScopedReader(ReplaceablePtr<T> *ptr) : ptr(ptr) { ptr->m.lock_shared(); }
    ~ScopedReader() { ptr->m.unlock_shared(); }

    const T *
    operator->()
    {
      return ptr->h.get();
    }

    const T *
    get()
    {
      return ptr->h.get();
    }

  private:
    ScopedReader(const ScopedReader &) = delete;
    ScopedReader &operator=(const ScopedReader &) = delete;

    ReplaceablePtr<T> *ptr;
  };

  // ScopedWriter constructs an object which is allowed to read and modify the
  // object pointed to by a ReplaceablePtr.
  //
  // The lifetime of the ReplaceablePtr must exceed the lifetime of this.
  //
  // An exclusive (write) lock is immediately acquired  upon construction of
  // this, and released upon destruction.
  class ScopedWriter
  {
  public:
    ScopedWriter(ReplaceablePtr<T> *ptr) : ptr(ptr) { ptr->m.lock(); }
    ~ScopedWriter() { ptr->m.unlock(); }

    T *
    operator->()
    {
      return ptr->h.get();
    }

    T *
    get()
    {
      return ptr->h.get();
    }

  private:
    ScopedWriter(const ScopedWriter &) = delete;
    ScopedWriter &operator=(const ScopedWriter &) = delete;

    ReplaceablePtr<T> *ptr;
  };

private:
  ReplaceablePtr(const ReplaceablePtr &) = delete;
  ReplaceablePtr &operator=(const ReplaceablePtr &) = delete;

  std::unique_ptr<T> h = nullptr;
  std::shared_mutex m;

  friend class ReplaceablePtr::ScopedReader;
};

class CacheHostTable
{
public:
  // Parameter name must not be deallocated before this
  //  object is
  CacheHostTable(Cache *c, CacheType typ);
  ~CacheHostTable();

  int BuildTable(const char *config_file_path);
  int BuildTableFromString(const char *config_file_path, char *str);

  void Match(const char *rdata, int rlen, CacheHostResult *result) const;
  void Print() const;

  int
  getEntryCount() const
  {
    return m_numEntries;
  }
  CacheHostMatcher *
  getHostMatcher() const
  {
    return hostMatch;
  }

  static int config_callback(const char *, RecDataT, RecData, void *);

  void
  register_config_callback(ReplaceablePtr<CacheHostTable> *p)
  {
    REC_RegisterConfigUpdateFunc("proxy.config.cache.hosting_filename", CacheHostTable::config_callback, (void *)p);
  }

  CacheType type   = CACHE_HTTP_TYPE;
  Cache *cache     = nullptr;
  int m_numEntries = 0;
  CacheHostRecord gen_host_rec;

private:
  CacheHostMatcher *hostMatch    = nullptr;
  const matcher_tags config_tags = {"hostname", "domain", nullptr, nullptr, nullptr, nullptr, false};
  const char *matcher_name       = "unknown"; // Used for Debug/Warning/Error messages
};

struct CacheHostTableConfig;
typedef int (CacheHostTableConfig::*CacheHostTabHandler)(int, void *);
struct CacheHostTableConfig : public Continuation {
  CacheHostTableConfig(ReplaceablePtr<CacheHostTable> *appt) : Continuation(nullptr), ppt(appt)
  {
    SET_HANDLER(&CacheHostTableConfig::mainEvent);
  }

  ~CacheHostTableConfig() {}

  int
  mainEvent(int event, Event *e)
  {
    (void)e;
    (void)event;

    CacheType type = CACHE_HTTP_TYPE;
    Cache *cache   = nullptr;
    {
      ReplaceablePtr<CacheHostTable>::ScopedReader hosttable(ppt);
      type  = hosttable->type;
      cache = hosttable->cache;
    }
    ppt->reset(new CacheHostTable(cache, type));
    delete this;
    return EVENT_DONE;
  }

private:
  ReplaceablePtr<CacheHostTable> *ppt;
};

/* list of volumes in the volume.config file */
struct ConfigVol {
  int number;
  CacheType scheme;
  off_t size;
  bool in_percent;
  bool ramcache_enabled;
  int percent;
  CacheVol *cachep;
  LINK(ConfigVol, link);
};

struct ConfigVolumes {
  int num_volumes;
  int num_http_volumes;
  Queue<ConfigVol> cp_queue;
  void read_config_file();
  void BuildListFromString(char *config_file_path, char *file_buf);

  void
  clear_all()
  {
    // remove all the volumes from the queue
    for (int i = 0; i < num_volumes; i++) {
      cp_queue.pop();
    }
    // reset count variables
    num_volumes      = 0;
    num_http_volumes = 0;
  }
};
