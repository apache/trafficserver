/** @file

  A cache (with map-esque interface) for RefCountObjs

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
#ifndef _P_RefCountCache_h_
#define _P_RefCountCache_h_

#include <I_EventSystem.h>
#include <P_EventSystem.h> // TODO: less? just need ET_TASK

#include <ts/Map.h>
#include <ts/PriorityQueue.h>

#include <ts/List.h>
#include <ts/ink_hrtime.h>

#include <ts/Vec.h>
#include <ts/I_Version.h>
#include <unistd.h>

#define REFCOUNT_CACHE_EVENT_SYNC REFCOUNT_CACHE_EVENT_EVENTS_START

#define REFCOUNTCACHE_MAGIC_NUMBER 0x0BAD2D9
#define REFCOUNTCACHE_MAJOR_VERSION 1
#define REFCOUNTCACHE_MINOR_VERSION 0

// Stats
enum RefCountCache_Stats {
  refcountcache_current_items_stat,        // current number of items
  refcountcache_current_size_stat,         // current size of cache
  refcountcache_total_inserts_stat,        // total items inserted
  refcountcache_total_failed_inserts_stat, // total items unable to insert
  refcountcache_total_lookups_stat,        // total get() calls
  refcountcache_total_hits_stat,           // total hits

  // Persistence metrics
  refcountcache_last_sync_time,   // seconds since epoch of last successful sync
  refcountcache_last_total_items, // number of items sync last time
  refcountcache_last_total_size,  // total size at last sync

  RefCountCache_Stat_Count
};

struct RefCountCacheItemMeta {
  uint64_t key;
  unsigned int size;
  ink_time_t expiry_time; // expire time as seconds since epoch
  RefCountCacheItemMeta(uint64_t key, unsigned int size, ink_time_t expiry_time = -1)
    : key(key), size(size), expiry_time(expiry_time)
  {
  }
};

// Layer of indirection for the hashmap-- since it needs lots of things inside of it
// We'll also use this as the item header, for persisting objects to disk
class RefCountCacheHashEntry
{
public:
  Ptr<RefCountObj> item;
  LINK(RefCountCacheHashEntry, item_link);
  PriorityQueueEntry<RefCountCacheHashEntry *> *expiry_entry;
  RefCountCacheItemMeta meta;

  void
  set(RefCountObj *i, uint64_t key, unsigned int size, int expire_time)
  {
    this->item = make_ptr(i);
    this->meta = RefCountCacheItemMeta(key, size, expire_time);
  };
  // Need a no-argument constructor to use the classAllocator
  RefCountCacheHashEntry() : item(Ptr<RefCountObj>()), expiry_entry(NULL), meta(0, 0) {}
  // make these values comparable -- so we can sort them
  bool
  operator<(const RefCountCacheHashEntry &v2) const
  {
    return this->meta.expiry_time < v2.meta.expiry_time;
  };
};
// Since the hashing values are all fixed size, we can simply use a classAllocator to avoid mallocs
extern ClassAllocator<RefCountCacheHashEntry> refCountCacheHashingValueAllocator;
extern ClassAllocator<PriorityQueueEntry<RefCountCacheHashEntry *>> expiryQueueEntry;

struct RefCountCacheHashing {
  typedef uint64_t ID;
  typedef uint64_t const Key;
  typedef RefCountCacheHashEntry Value;
  typedef DList(RefCountCacheHashEntry, item_link) ListHead;

  static ID
  hash(Key key)
  {
    return key;
  }
  static Key
  key(Value const *value)
  {
    return value->meta.key;
  }
  static bool
  equal(Key lhs, Key rhs)
  {
    return lhs == rhs;
  }
};

// The RefCountCachePartition is simply a map of key -> Ptr<YourClass>
// We partition the cache to reduce lock contention
template <class C> class RefCountCachePartition
{
public:
  RefCountCachePartition(unsigned int part_num, uint64_t max_size, unsigned int max_items, RecRawStatBlock *rsb = NULL);
  Ptr<C> get(uint64_t key);
  void put(uint64_t key, C *item, int size = 0, int expire_time = 0);
  void erase(uint64_t key, ink_time_t expiry_time = -1);

  void clear();
  bool is_full() const;
  bool make_space_for(unsigned int);

  size_t count() const;
  void copy(Vec<RefCountCacheHashEntry *> &items);

  typedef typename TSHashTable<RefCountCacheHashing>::iterator iterator_type;
  typedef typename TSHashTable<RefCountCacheHashing>::self hash_type;
  typedef typename TSHashTable<RefCountCacheHashing>::Location location_type;
  TSHashTable<RefCountCacheHashing> *get_map();

  Ptr<ProxyMutex> lock; // Lock

private:
  void metric_inc(RefCountCache_Stats metric_enum, int64_t data);

  unsigned int part_num;
  uint64_t max_size;
  unsigned int max_items;
  uint64_t size;
  unsigned int items;

  hash_type item_map;

  PriorityQueue<RefCountCacheHashEntry *> expiry_queue;
  RecRawStatBlock *rsb;
};

template <class C>
RefCountCachePartition<C>::RefCountCachePartition(unsigned int part_num, uint64_t max_size, unsigned int max_items,
                                                  RecRawStatBlock *rsb)
  : lock(new_ProxyMutex()), part_num(part_num), max_size(max_size), max_items(max_items), size(0), items(0), rsb(rsb)
{
}

template <class C>
Ptr<C>
RefCountCachePartition<C>::get(uint64_t key)
{
  this->metric_inc(refcountcache_total_lookups_stat, 1);
  location_type l = this->item_map.find(key);
  if (l.isValid()) {
    // found
    this->metric_inc(refcountcache_total_hits_stat, 1);
    return make_ptr((C *)l.m_value->item.get());
  } else {
    return Ptr<C>();
  }
}

template <class C>
void
RefCountCachePartition<C>::put(uint64_t key, C *item, int size, int expire_time)
{
  this->metric_inc(refcountcache_total_inserts_stat, 1);
  size += sizeof(C);
  // Remove any colliding entries
  this->erase(key);

  // if we are full, and can't make space-- then don't store the item
  if (this->is_full() && !this->make_space_for(size)) {
    Debug("refcountcache", "partition %d is full-- not storing item key=%" PRIu64, this->part_num, key);
    this->metric_inc(refcountcache_total_failed_inserts_stat, 1);
    return;
  }

  // Create our value-- which has a ref to the `item`
  RefCountCacheHashEntry *val = refCountCacheHashingValueAllocator.alloc();
  val->set(item, key, size, expire_time);

  // add expiry_entry to expiry queue, if the expire time is positive (otherwise it means don't expire)
  if (expire_time >= 0) {
    Debug("refcountcache", "partition %d adding entry with expire_time=%d\n", this->part_num, expire_time);
    PriorityQueueEntry<RefCountCacheHashEntry *> *expiry_entry = expiryQueueEntry.alloc();
    new ((void *)expiry_entry) PriorityQueueEntry<RefCountCacheHashEntry *>(val);
    expiry_queue.push(expiry_entry);
    val->expiry_entry = expiry_entry;
  }

  // add the item to the map
  this->item_map.insert(val);
  this->size += val->meta.size;
  this->items++;
  this->metric_inc(refcountcache_current_size_stat, (int64_t)val->meta.size);
  this->metric_inc(refcountcache_current_items_stat, 1);
}

template <class C>
void
RefCountCachePartition<C>::erase(uint64_t key, ink_time_t expiry_time)
{
  location_type l = this->item_map.find(key);
  if (l.isValid()) {
    if (expiry_time >= 0 && l.m_value->meta.expiry_time != expiry_time) {
      return;
    }
    // TSHashMap does NOT clean up the item-- this remove just removes it from the map
    // we are responsible for cleaning it up here
    this->item_map.remove(l);

    // decrement usage counters
    this->size -= l.m_value->meta.size;
    this->items--;

    this->metric_inc(refcountcache_current_size_stat, -((int64_t)l.m_value->meta.size));
    this->metric_inc(refcountcache_current_items_stat, -1);

    // remove from expiry queue
    if (l.m_value->expiry_entry != NULL) {
      Debug("refcountcache", "partition %d deleting item from expiry_queue idx=%d\n", this->part_num,
            l.m_value->expiry_entry->index);
      this->expiry_queue.erase(l.m_value->expiry_entry);
      expiryQueueEntry.free(l.m_value->expiry_entry);
      l.m_value->expiry_entry = NULL; // To avoid the destruction of `l` calling the destructor again-- and causing issues
    }
    // Since the Value is actually RefCountObj-- when this gets deleted normally it calls the wrong
    // `free` method, this forces the delete/decr to happen with the right type
    Ptr<C> *tmp = (Ptr<C> *)&l.m_value->item;
    tmp->clear();
    l.m_value->~RefCountCacheHashEntry();
    refCountCacheHashingValueAllocator.free(l.m_value);
  }
}

template <class C>
void
RefCountCachePartition<C>::clear()
{
  // this->item_map.clear() doesn't clean up anything, so instead of using that we'll iterate
  // over every item and then call delete
  for (RefCountCachePartition<C>::iterator_type i = this->item_map.begin(); i != this->item_map.end(); ++i) {
    this->erase(i.m_value->meta.key, i.m_value->meta.expiry_time);
  }
}

// Are we full?
template <class C>
bool
RefCountCachePartition<C>::is_full() const
{
  Debug("refcountcache", "partition %d is full? items %d/%d size %" PRIu64 "/%" PRIu64 "\n\n", this->part_num, this->items,
        this->max_items, this->size, this->max_size);
  return (this->max_items > 0 && this->items >= this->max_items) || (this->max_size > 0 && this->size >= this->max_size);
}

// Attempt to make space for item of `size`
template <class C>
bool
RefCountCachePartition<C>::make_space_for(unsigned int size)
{
  ink_time_t now = ink_time();
  while (this->is_full() || (size > 0 && this->size + size > this->max_size)) {
    PriorityQueueEntry<RefCountCacheHashEntry *> *top_item = expiry_queue.top();
    // if there is nothing in the expiry queue, then we can't make space
    if (top_item == NULL) {
      return false;
    }

    // If the first item has expired, lets evict it, and then go around again
    if (top_item->node->meta.expiry_time < now) {
      this->erase(top_item->node->meta.key);
      expiry_queue.pop();
    } else { // if the first item isn't expired-- the rest won't be either (queue is sorted)
      return false;
    }
  }
  return true;
}

template <class C>
size_t
RefCountCachePartition<C>::count() const
{
  return this->items;
}

template <class C>
void
RefCountCachePartition<C>::copy(Vec<RefCountCacheHashEntry *> &items)
{
  for (RefCountCachePartition<C>::iterator_type i = this->item_map.begin(); i != this->item_map.end(); ++i) {
    RefCountCacheHashEntry *val = refCountCacheHashingValueAllocator.alloc();
    val->set(i.m_value->item.get(), i.m_value->meta.key, i.m_value->meta.size, i.m_value->meta.expiry_time);
    items.push_back(val);
  }
}

template <class C>
void
RefCountCachePartition<C>::metric_inc(RefCountCache_Stats metric_enum, int64_t data)
{
  if (this->rsb) {
    RecIncrGlobalRawStatCount(this->rsb, metric_enum, data);
  }
}

template <class C>
TSHashTable<RefCountCacheHashing> *
RefCountCachePartition<C>::get_map()
{
  return &this->item_map;
}

// The header for the cache, this is used to check if the serialized cache is compatible
class RefCountCacheHeader
{
public:
  unsigned int magic;
  VersionNumber version;
  VersionNumber object_version; // version passed in of whatever it is we are caching

  RefCountCacheHeader(VersionNumber object_version = VersionNumber());
  bool operator==(const RefCountCacheHeader other) const;
  bool compatible(RefCountCacheHeader *other) const;
};

// This continuation is responsible for persisting RefCountCache to disk
// To avoid locking the partitions for a long time we'll do the following per-partition:
//    - lock
//    - copy ptrs (bump refcount)
//    - unlock
//    - persist
//    - remove ptrs (drop refcount)
// This way we only have to hold the lock on the partition for the time it takes to get Ptr<>s to all items in the partition
template <class C> class RefCountCacheSerializer : public Continuation
{
public:
  size_t partition; // Current partition
  C *cache;         // Pointer to the entire cache
  Continuation *cont;

  int copy_partition(int event, Event *e);
  int write_partition(int event, Event *e);
  int pause_event(int event, Event *e);

  // Create the tmp file on disk we'll be writing to
  int initialize_storage(int event, Event *e);
  // do the final mv and close of file handle
  int finalize_sync();

  // helper method to spin on writes to disk
  int write_to_disk(const void *, size_t);

  RefCountCacheSerializer(Continuation *acont, C *cc, int frequency, std::string dirname, std::string filename);
  ~RefCountCacheSerializer();

private:
  Vec<RefCountCacheHashEntry *> partition_items;

  int fd; // fd for the file we are writing to

  std::string dirname;
  std::string filename;
  std::string tmp_filename;

  ink_hrtime time_per_partition;
  ink_hrtime start;

  int total_items;
  int64_t total_size;

  RecRawStatBlock *rsb;
};

template <class C>
int
RefCountCacheSerializer<C>::copy_partition(int /* event */, Event *e)
{
  if (partition >= cache->partition_count()) {
    int error = this->finalize_sync();
    if (error != 0) {
      Warning("Unable to finalize sync of cache to disk %s: %s", this->filename.c_str(), strerror(-error));
    }

    Debug("refcountcache", "RefCountCacheSync done");
    delete this;
    return EVENT_DONE;
  }

  Debug("refcountcache", "sync partition=%ld/%ld", partition, cache->partition_count());
  // copy the partition into our buffer, then we'll let `pauseEvent` write it out
  this->partition_items.reserve(cache->get_partition(partition).count());
  cache->get_partition(partition).copy(this->partition_items);
  partition++;

  SET_HANDLER(&RefCountCacheSerializer::write_partition);
  mutex = e->ethread->mutex;
  e->schedule_imm(ET_TASK);

  return EVENT_CONT;
}

template <class C>
int
RefCountCacheSerializer<C>::write_partition(int /* event */, Event *e)
{
  int curr_time = Thread::get_hrtime() / HRTIME_SECOND;

  // write the partition to disk
  // for item in this->partitionItems
  // write to disk with headers per item

  for (unsigned int i = 0; i < this->partition_items.length(); i++) {
    RefCountCacheHashEntry *it = this->partition_items[i];

    // check if the item has expired, if so don't persist it to disk
    if (it->meta.expiry_time < curr_time) {
      continue;
    }

    // Write the RefCountCacheItemMeta (as our header)
    int ret = this->write_to_disk((char *)&it->meta, sizeof(it->meta));
    if (ret < 0) {
      Warning("Error writing cache item header to %s: %d", this->tmp_filename.c_str(), ret);
      delete this;
      return EVENT_DONE;
    }

    // write the actual object now
    ret = this->write_to_disk((char *)it->item.get(), it->meta.size);
    if (ret < 0) {
      Warning("Error writing cache item to %s: %d", this->tmp_filename.c_str(), ret);
      delete this;
      return EVENT_DONE;
    }

    this->total_items++;
    this->total_size += it->meta.size;
    refCountCacheHashingValueAllocator.free(it);
  }

  // Clear partition-- for the next user
  this->partition_items.clear();

  SET_HANDLER(&RefCountCacheSerializer::pause_event);

  // Figure out how much time we spent
  ink_hrtime elapsed          = Thread::get_hrtime() - this->start;
  ink_hrtime expected_elapsed = (this->partition * this->time_per_partition);

  // If we were quicker than our pace-- lets reschedule in the future
  if (elapsed < expected_elapsed) {
    e->schedule_in(expected_elapsed - elapsed, ET_TASK);
  } else { // Otherwise we were too slow-- and need to go now!
    e->schedule_imm(ET_TASK);
  }
  return EVENT_CONT;
}

template <class C>
int
RefCountCacheSerializer<C>::pause_event(int /* event */, Event *e)
{
  // Schedule up the next partition
  if (partition < cache->partition_count()) {
    mutex = cache->get_partition(partition).lock.get();
  } else {
    mutex = cont->mutex;
  }

  SET_HANDLER(&RefCountCacheSerializer::copy_partition);
  e->schedule_imm(ET_TASK);
  return EVENT_CONT;
}

// Open the tmp file, etc.
template <class C>
int
RefCountCacheSerializer<C>::initialize_storage(int /* event */, Event *e)
{
  this->fd = socketManager.open(this->tmp_filename.c_str(), O_TRUNC | O_RDWR | O_CREAT, 0644); // TODO: configurable perms
  if (this->fd == -1) {
    Warning("Unable to create temporary file %s, unable to persist hostdb: %d :%s\n", this->tmp_filename.c_str(), this->fd,
            strerror(errno));
    delete this;
    return EVENT_DONE;
  }

  // Write out the header
  int ret = this->write_to_disk((char *)&this->cache->get_header(), sizeof(RefCountCacheHeader));
  if (ret < 0) {
    Warning("Error writing cache header to %s: %d", this->tmp_filename.c_str(), ret);
    delete this;
    return EVENT_DONE;
  }

  SET_HANDLER(&RefCountCacheSerializer::pause_event);
  e->schedule_imm(ET_TASK);
  return EVENT_CONT;
}

// Do the final mv and close of file handle. Only reset "fd" to -1 if we fully succeed.
// Returns 0 on success, -errno on failure.
template <class C>
int
RefCountCacheSerializer<C>::finalize_sync()
{
  int error; // Socket manager return 0 or -errno.
  int dirfd = -1;

  // fsync the fd we have
  if ((error = socketManager.fsync(this->fd))) {
    return error;
  }

  dirfd = socketManager.open(this->dirname.c_str(), O_DIRECTORY);
  if (dirfd == -1) {
    return -errno;
  }

  // Rename from the temp name to the real name.
  if (rename(this->tmp_filename.c_str(), this->filename.c_str()) != 0) {
    error = -errno;
    socketManager.close(dirfd);
    return error;
  }

  // Fsync the directory to persist the rename.
  if ((error = socketManager.fsync(dirfd))) {
    socketManager.close(dirfd);
    return error;
  }

  // Don't bother checking for errors on the close since theere's nothing we can do about it at
  // this point anyway.
  socketManager.close(dirfd);
  socketManager.close(this->fd);
  this->fd = -1;

  if (this->rsb) {
    RecSetRawStatCount(this->rsb, refcountcache_last_sync_time, Thread::get_hrtime() / HRTIME_SECOND);
    RecSetRawStatCount(this->rsb, refcountcache_last_total_items, this->total_items);
    RecSetRawStatCount(this->rsb, refcountcache_last_total_size, this->total_size);
  }

  return 0;
}

// Write *i to this->fd, if there is an error we'll just stop this continuation
// TODO: reschedule the continuation if the disk was busy?
template <class C>
int
RefCountCacheSerializer<C>::write_to_disk(const void *ptr, size_t n_bytes)
{
  size_t written = 0;
  while (written < n_bytes) {
    int ret = socketManager.write(this->fd, (char *)ptr + written, n_bytes - written);
    if (ret <= 0) {
      return -1;
    } else {
      written += ret;
    }
  }
  return 0;
}

template <class C>
RefCountCacheSerializer<C>::RefCountCacheSerializer(Continuation *acont, C *cc, int frequency, std::string dirname,
                                                    std::string filename)
  : Continuation(NULL),
    partition(0),
    cache(cc),
    cont(acont),
    fd(-1),
    dirname(dirname),
    filename(filename),
    time_per_partition(HRTIME_SECONDS(frequency) / cc->partition_count()),
    start(Thread::get_hrtime()),
    total_items(0),
    total_size(0),
    rsb(cc->get_rsb())
{
  eventProcessor.schedule_imm(this, ET_TASK);
  this->tmp_filename = this->filename + ".syncing"; // TODO tmp file extension configurable?

  Debug("refcountcache", "started serializer %p", this);
  SET_HANDLER(&RefCountCacheSerializer::initialize_storage);
}

template <class C> RefCountCacheSerializer<C>::~RefCountCacheSerializer()
{
  // If we failed before finalizing the on-disk copy, close up and nuke the temporary sync file.
  if (this->fd != -1) {
    unlink(this->tmp_filename.c_str());
    socketManager.close(fd);
  }

  Debug("refcountcache", "finished serializer %p", this);

  // Note that we have to do the unlink before we send the completion event, otherwise
  // we could unlink the sync file out from under another serializer.
  cont->handleEvent(REFCOUNT_CACHE_EVENT_SYNC, 0);
}

// RefCountCache is a ref-counted key->value map to store classes that inherit from RefCountObj.
// Once an item is `put` into the cache, the cache will maintain a Ptr<> to that object until erase
// or clear is called-- which will remove the cache's Ptr<> to the object.
//
// This cache may be Persisted (RefCountCacheSync) as well as loaded from disk (LoadRefCountCacheFromPath).
// This class will optionally emit metrics at the given `metrics_prefix`.
//
// Note: although this cache does allow you to set expiry times this cache does not actively GC itself-- meaning
// it will only remove expired items once the space is required. So to ensure that the cache is bounded either a
// size or an item limit must be set-- otherwise the cache will not GC.
//
// Also note, that if keys collide the previous
// entry for a given key will be removed, so this "leak" concern is assuming you don't have sufficient space to store
// an item for each possible key
template <class C> class RefCountCache
{
public:
  // Constructor
  RefCountCache(unsigned int num_partitions, int size = -1, int items = -1, VersionNumber object_version = VersionNumber(),
                std::string metrics_prefix = "");
  // Destructor
  ~RefCountCache();

  // User interface to the cache
  Ptr<C> get(uint64_t key);
  void put(uint64_t key, C *item, int size = 0, ink_time_t expiry_time = -1);
  void erase(uint64_t key);
  void clear();

  // Some methods to get some internal state
  int partition_for_key(uint64_t key);
  ProxyMutex *lock_for_key(uint64_t key);
  size_t partition_count() const;
  RefCountCachePartition<C> &get_partition(int pnum);
  size_t count() const;
  RefCountCacheHeader &get_header();
  RecRawStatBlock *get_rsb();

private:
  int max_size;  // Total size
  int max_items; // Total number of items allowed
  unsigned int num_partitions;
  Vec<RefCountCachePartition<C> *> partitions;
  // Header
  RefCountCacheHeader header; // Our header
  RecRawStatBlock *rsb;
};

template <class C>
RefCountCache<C>::RefCountCache(unsigned int num_partitions, int size, int items, VersionNumber object_version,
                                std::string metrics_prefix)
  : header(RefCountCacheHeader(object_version)), rsb(NULL)
{
  this->max_size       = size;
  this->max_items      = items;
  this->num_partitions = num_partitions;

  if (metrics_prefix.length() > 0) {
    this->rsb = RecAllocateRawStatBlock((int)RefCountCache_Stat_Count);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "current_items").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_current_items_stat, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "current_size").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_current_size_stat, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "total_inserts").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_total_inserts_stat, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "total_failed_inserts").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_total_failed_inserts_stat, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "total_lookups").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_total_lookups_stat, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "total_hits").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_total_hits_stat, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "last_sync.time").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_last_sync_time, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "last_sync.total_items").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_last_total_items, RecRawStatSyncCount);

    RecRegisterRawStat(this->rsb, RECT_PROCESS, (metrics_prefix + "last_sync.total_size").c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)refcountcache_last_total_size, RecRawStatSyncCount);
  }
  // Now lets create all the partitions
  this->partitions.reserve(num_partitions);
  for (unsigned int i = 0; i < num_partitions; i++) {
    this->partitions.push_back(new RefCountCachePartition<C>(i, size / num_partitions, items / num_partitions, this->rsb));
  }
}

// Deconstruct the class
template <class C> RefCountCache<C>::~RefCountCache()
{
  delete this->partitions;
}

template <class C>
Ptr<C>
RefCountCache<C>::get(uint64_t key)
{
  return this->partitions[this->partition_for_key(key)]->get(key);
}

template <class C>
void
RefCountCache<C>::put(uint64_t key, C *item, int size, ink_time_t expiry_time)
{
  return this->partitions[this->partition_for_key(key)]->put(key, item, size, expiry_time);
}

// Pick a partition for a given item
template <class C>
int
RefCountCache<C>::partition_for_key(uint64_t key)
{
  return key % this->num_partitions;
}

template <class C>
RefCountCacheHeader &
RefCountCache<C>::get_header()
{
  return this->header;
}

template <class C>
ProxyMutex *
RefCountCache<C>::lock_for_key(uint64_t key)
{
  return this->partitions[this->partition_for_key(key)]->lock.get();
}

template <class C>
RefCountCachePartition<C> &
RefCountCache<C>::get_partition(int pnum)
{
  return *this->partitions[pnum];
}

template <class C>
size_t
RefCountCache<C>::count() const
{
  size_t c = 0;
  for (unsigned int i = 0; i < this->num_partitions; i++) {
    c += this->partitions[i]->count();
  }
  return c;
}

template <class C>
size_t
RefCountCache<C>::partition_count() const
{
  return this->num_partitions;
}

template <class C>
RecRawStatBlock *
RefCountCache<C>::get_rsb()
{
  return this->rsb;
}

template <class C>
void
RefCountCache<C>::erase(uint64_t key)
{
  this->partitions[this->partition_for_key(key)]->erase(key);
}

template <class C>
void
RefCountCache<C>::clear()
{
  for (unsigned int i = 0; i < this->num_partitions; i++) {
    this->partitions[i]->clear();
  }
}

// Fill `cache` with items in file `filepath` using `load_func` to unmarshall the record.
// Errors are -1
template <typename CacheEntryType>
int
LoadRefCountCacheFromPath(RefCountCache<CacheEntryType> &cache, std::string dirname, std::string filepath,
                          CacheEntryType *(*load_func)(char *, unsigned int))
{
  // If we have no load method, then we can't load anything so lets just stop right here
  if (load_func == NULL) {
    return -1; // TODO: some specific error code
  }

  int fd = open(filepath.c_str(), O_RDONLY);
  if (fd < 0) {
    return -1; // specific code for missing?
  }

  // read in the header
  RefCountCacheHeader tmpHeader = RefCountCacheHeader();
  int read_ret                  = read(fd, (char *)&tmpHeader, sizeof(RefCountCacheHeader));
  if (read_ret != sizeof(RefCountCacheHeader)) {
    socketManager.close(fd);
    Warning("Error reading cache header from disk (expected %ld): %d", sizeof(RefCountCacheHeader), read_ret);
    return -1;
  }
  if (!cache.get_header().compatible(&tmpHeader)) {
    socketManager.close(fd);
    Warning("Incompatible cache at %s, not loading.", filepath.c_str());
    return -1; // TODO: specific code for incompatible
  }

  RefCountCacheItemMeta tmpValue = RefCountCacheItemMeta(0, 0);
  while (true) { // TODO: better loop
    read_ret = read(fd, (char *)&tmpValue, sizeof(tmpValue));
    if (read_ret != sizeof(tmpValue)) {
      break;
    }
    char buf[tmpValue.size];
    read_ret = read(fd, (char *)&buf, tmpValue.size);
    if (read_ret != (int)tmpValue.size) {
      Warning("Encountered error reading item from cache: %d", read_ret);
      break;
    }

    CacheEntryType *newItem = load_func((char *)&buf, tmpValue.size);
    if (newItem != NULL) {
      cache.put(tmpValue.key, newItem, tmpValue.size - sizeof(CacheEntryType));
    }
  };

  socketManager.close(fd);
  return 0;
}

#endif /* _P_RefCountCache_h_ */
