/** @file
 *
 *  Persistence interface for HostDB RefCountCache.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "P_RefCountCache.h"

#include <utility>
#include <vector>

// This continuation is responsible for persisting RefCountCache to disk
// To avoid locking the partitions for a long time we'll do the following per-partition:
//    - lock
//    - copy ptrs (bump refcount)
//    - unlock
//    - persist
//    - remove ptrs (drop refcount)
//
// This way we only have to hold the lock on the partition for the
// time it takes to get Ptr<>s to all items in the partition
template <class C> class RefCountCacheSerializer : public Continuation
{
public:
  size_t partition;        // Current partition
  RefCountCache<C> *cache; // Pointer to the entire cache
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

  RefCountCacheSerializer(Continuation *acont, RefCountCache<C> *cc, int frequency, std::string dirname, std::string filename);
  ~RefCountCacheSerializer() override;

private:
  std::vector<RefCountCacheHashEntry *> partition_items;

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
RefCountCacheSerializer<C>::RefCountCacheSerializer(Continuation *acont, RefCountCache<C> *cc, int frequency, std::string dirname,
                                                    std::string filename)
  : Continuation(nullptr),
    partition(0),
    cache(cc),
    cont(acont),
    fd(-1),
    dirname(std::move(dirname)),
    filename(std::move(filename)),
    time_per_partition(HRTIME_SECONDS(frequency) / cc->partition_count()),
    start(ink_get_hrtime()),
    total_items(0),
    total_size(0),
    rsb(cc->get_rsb())
{
  this->tmp_filename = this->filename + ".syncing"; // TODO tmp file extension configurable?

  Debug("refcountcache", "started serializer %p", this);
  SET_HANDLER(&RefCountCacheSerializer::initialize_storage);
  eventProcessor.schedule_imm(this, ET_TASK);
}

template <class C> RefCountCacheSerializer<C>::~RefCountCacheSerializer()
{
  // If we failed before finalizing the on-disk copy, close up and nuke the temporary sync file.
  if (this->fd != -1) {
    unlink(this->tmp_filename.c_str());
    socketManager.close(fd);
  }

  for (auto &entry : this->partition_items) {
    RefCountCacheHashEntry::free<C>(entry);
  }
  this->partition_items.clear();

  Debug("refcountcache", "finished serializer %p", this);

  // Note that we have to do the unlink before we send the completion event, otherwise
  // we could unlink the sync file out from under another serializer.

  // Schedule off the REFCOUNT event, so the continuation gets properly locked
  this_ethread()->schedule_imm(cont, REFCOUNT_CACHE_EVENT_SYNC);
}

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
  int curr_time = ink_get_hrtime() / HRTIME_SECOND;

  // write the partition to disk
  // for item in this->partitionItems
  // write to disk with headers per item

  for (unsigned int i = 0; i < this->partition_items.size(); i++) {
    RefCountCacheHashEntry *entry = this->partition_items[i];

    // check if the item has expired, if so don't persist it to disk
    if (entry->meta.expiry_time < curr_time) {
      continue;
    }

    // Write the RefCountCacheItemMeta (as our header)
    int ret = this->write_to_disk((char *)&entry->meta, sizeof(entry->meta));
    if (ret < 0) {
      Warning("Error writing cache item header to %s: %s", this->tmp_filename.c_str(), strerror(-ret));
      delete this;
      return EVENT_DONE;
    }

    // write the actual object now
    ret = this->write_to_disk((char *)entry->item.get(), entry->meta.size);
    if (ret < 0) {
      Warning("Error writing cache item to %s: %s", this->tmp_filename.c_str(), strerror(-ret));
      delete this;
      return EVENT_DONE;
    }

    this->total_items++;
    this->total_size += entry->meta.size;
  }

  // Clear the copied partition for the next round.
  for (auto &entry : this->partition_items) {
    RefCountCacheHashEntry::free<C>(entry);
  }
  this->partition_items.clear();

  SET_HANDLER(&RefCountCacheSerializer::pause_event);

  // Figure out how much time we spent
  ink_hrtime elapsed          = ink_get_hrtime() - this->start;
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
  if (this->fd < 0) {
    Warning("Unable to create temporary file %s, unable to persist hostdb: %s", this->tmp_filename.c_str(), strerror(errno));
    delete this;
    return EVENT_DONE;
  }

  // Write out the header
  int ret = this->write_to_disk((char *)&this->cache->get_header(), sizeof(RefCountCacheHeader));
  if (ret < 0) {
    Warning("Error writing cache header to %s: %s", this->tmp_filename.c_str(), strerror(-ret));
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

#ifdef O_DIRECTORY
  dirfd = socketManager.open(this->dirname.c_str(), O_DIRECTORY);
#else
  struct stat st;
  stat(this->dirname.c_str(), &st);
  if (!S_ISDIR(st.st_mode)) {
    return -ENOTDIR;
  }
  dirfd = socketManager.open(this->dirname.c_str(), 0);
#endif
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

  // Don't bother checking for errors on the close since there's nothing we can do about it at
  // this point anyway.
  socketManager.close(dirfd);
  socketManager.close(this->fd);
  this->fd = -1;

  if (this->rsb) {
    RecSetRawStatCount(this->rsb, refcountcache_last_sync_time, ink_get_hrtime() / HRTIME_SECOND);
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
      return ret;
    } else {
      written += ret;
    }
  }
  return 0;
}
