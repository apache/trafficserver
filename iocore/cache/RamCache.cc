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
  RamCache.h - a fast, simple object RAM cache
  
 ****************************************************************************/
#include "P_Cache.h"

ClassAllocator<RamCacheEntry> ramCacheEntryAllocator("RamCacheEntry");

inline RamCacheEntry *
new_RamCacheEntry(EThread * t)
{
  return THREAD_ALLOC(ramCacheEntryAllocator, t);
}
inline void
free_RamCacheEntry(RamCacheEntry * e, EThread * t)
{
  e->data = NULL;
  THREAD_FREE(e, ramCacheEntryAllocator, t);
}

void
RamCache::init(ink64 abytes, ink64 aobjects, int cutoff, Part * _part, ProxyMutex * m)
{
  partition_size = aobjects / n_partitions;
  cutoff_size = cutoff;

  Debug("ram_cache", "initializing ram_cache, partition_size=%d, aobjects=%d, abytes=%d",
        partition_size, aobjects, abytes);
  /* equivalent to ram cache disabled */
  if (partition_size == 0)
    return;

  seen_size = (partition_size * RAM_CACHE_SEEN_PER_OBJECT) - 1;
  bytes = abytes / n_partitions;
  objects = aobjects / n_partitions;
  part = _part;

  if (n_partitions != 1)
    partition = NEW(new RamCachePartition[n_partitions]);
  else
    partition = &one_partition;
  int s = partition_size * sizeof(DLL<RamCacheEntry>);
  for (int i = 0; i < n_partitions; i++) {
    partition[i].bucket = (DLL<RamCacheEntry> *)xmalloc(s);
    memset(partition[i].bucket, 0, s);
    if (m)
      partition[i].lock = m;
    else
      partition[i].lock = new_ProxyMutex();
    partition[i].seen = (unsigned short *) xmalloc(seen_size * sizeof(unsigned short));
    memset(partition[i].seen, 0, seen_size * sizeof(unsigned short));
  }
}

int
RamCache::get(INK_MD5 * key, Ptr<IOBufferData> *ret_data, inku32 auxkey1, inku32 auxkey2)
{
  /* equivalent to ram cache disabled */
  if (partition_size == 0)
    return 0;

  inku32 k = key->word(2);
  inku32 pp = k % n_partitions;
  RamCachePartition *p = &partition[pp];
  inku32 o = k / n_partitions;
  inku32 i = o % partition_size;
  RamCacheEntry *e = p->bucket[i].head;
  while (e) {
    if (e->key == *key && e->auxkey1 == auxkey1 && e->auxkey2 == auxkey2) {
      p->lru.remove(e, e->lru_link);
      p->lru.enqueue(e, e->lru_link);
      (*ret_data) = e->data;
      Debug("ram_cache", "get %X %d %d HIT", k, auxkey1, auxkey2);
      return 1;
    }
    e = e->hash_link.next;
  }
  Debug("ram_cache", "get %X %d %d MISS", k, auxkey1, auxkey2);
  return 0;
}

int
RamCache::get_lock(INK_MD5 * key, Ptr<IOBufferData> *ret_data, EThread * t, inku32 auxkey1, inku32 auxkey2)
{
  inku32 k = key->word(2);
  int pp = k % n_partitions;
  RamCachePartition *p = &partition[pp];
  (void) p;
  MUTEX_TRY_LOCK(l, p->lock, t);
  if (!l)
    return -1;
  return get(key, ret_data, auxkey1, auxkey2);
}

void
RamCache::remove_entry(RamCacheEntry * ee, RamCachePartition * p, EThread * t)
{
  inku32 oo = ee->key.word(2) / n_partitions;
  inku32 ii = oo % partition_size;
  p->bucket[ii].remove(ee, ee->hash_link);
  p->cur_bytes -= ee->data->block_size();
  ProxyMutex *mutex = part->mutex;
  CACHE_SUM_DYN_STAT(cache_ram_cache_bytes_stat, -ee->data->block_size());
  Debug("ram_cache", "put %X %d %d FREED", ee->key.word(2), ee->auxkey1, ee->auxkey2);
  free_RamCacheEntry(ee, t);
}

int
RamCache::put(INK_MD5 * key, IOBufferData * data, EThread * t, inku32 auxkey1, inku32 auxkey2)
{

  /* equivalent to ram cache disabled */
  if (partition_size == 0)
    return 0;

  ProxyMutex *mutex = t->mutex;
  (void) mutex;
  inku32 k = key->word(2);
  inku32 pp = k % n_partitions;
  inku32 o = k / n_partitions;
  inku32 i = o % partition_size;
  inku32 s = o % seen_size;
  RamCachePartition *p = &partition[pp];

  inku32 k3 = key->word(3);
  unsigned short oldseen = p->seen[s];
  p->seen[s] = (unsigned short) k3;
  if ((oldseen != (unsigned short) k3) && bytes <= p->cur_bytes + RAM_CACHE_FAST_LOAD_SIZE) {
    Debug("ram_cache", "put %X %d %d FIRST SEEN", k, auxkey1, auxkey2);
    return 0;
  }

  RamCacheEntry *e = p->bucket[i].head;
  while (e) {
    RamCacheEntry *n = e->hash_link.next;
    if (e->key == *key) {
      if (e->auxkey1 == auxkey1 && e->auxkey2 == auxkey2) {
        Debug("ram_cache", "put %X %d %d PRESENT", k, auxkey1, auxkey2);
        return 1;               // already present
      } else {
        p->lru.remove(e, e->lru_link);
        remove_entry(e, p, t);
      }
    }
    e = n;
  }
  e = new_RamCacheEntry(t);
  e->key = *key;
  e->auxkey1 = auxkey1;
  e->auxkey2 = auxkey2;
  e->data = data;
  ink_assert(p->bucket[i].head != e);
  p->bucket[i].push(e, e->hash_link);
  ink_assert(e->hash_link.next != e);
  p->lru.enqueue(e, e->lru_link);
  p->cur_bytes += data->block_size();
  CACHE_SUM_DYN_STAT(cache_ram_cache_bytes_stat, data->block_size());
  while (p->cur_bytes > bytes) {
    RamCacheEntry *ee = p->lru.dequeue(p->lru.head, p->lru.head->lru_link);
    if (ee)
      remove_entry(ee, p, t);
    else
      break;
  }
  Debug("ram_cache", "put %X %d %d INSERTED", k, auxkey1, auxkey2);
  return 1;
}

int
RamCache::put_lock(INK_MD5 * key, IOBufferData * data, EThread * t, inku32 auxkey1, inku32 auxkey2)
{
  inku32 k = key->word(2);
  int pp = k % n_partitions;
  RamCachePartition *p = &partition[pp];
  (void) p;
  MUTEX_TRY_LOCK(l, p->lock, t);
  if (!l)
    return -1;
  return put(key, data, t, auxkey1, auxkey2);
}

int
RamCache::fixup(INK_MD5 * key, inku32 old_auxkey1, inku32 old_auxkey2, inku32 new_auxkey1, inku32 new_auxkey2)
{
  /* equivalent to ram cache disabled */
  if (partition_size == 0)
    return 0;
  Debug("ram_cache", "fixup %d", key);
  inku32 k = key->word(2);
  inku32 pp = k % n_partitions;
  RamCachePartition *p = &partition[pp];
  inku32 o = k / n_partitions;
  inku32 i = o % partition_size;
  RamCacheEntry *e = p->bucket[i].head;
  while (e) {
    if (e->key == *key && e->auxkey1 == old_auxkey1 && e->auxkey2 == old_auxkey2) {
      e->auxkey1 = new_auxkey1;
      e->auxkey2 = new_auxkey2;
      return 1;
    }
    e = e->hash_link.next;
  }
  return 0;
}

void
RamCache::print_stats(FILE * fp, int verbose)
{
  fprintf(fp, "RAM Cache <%X>\n", (unsigned int) this);
  fprintf(fp, "\tn_partitions: %d\n", n_partitions);
  fprintf(fp, "\tbytes: %d\n", (int) bytes);
  fprintf(fp, "\tobjects: %d\n", (int) objects);
  fprintf(fp, "\tparitition_size: %d\n", (int) partition_size);
  fprintf(fp, "\tseen_size: %d\n", (int) seen_size);
  fprintf(fp, "\tcutoff_size: %d\n", cutoff_size);
  for (int i = 0; i < n_partitions; i++) {
    fprintf(fp, "\tPartition: %d\n", i);
    fprintf(fp, "\t\tcur_bytes: %d\n", partition[i].cur_bytes);
    fprintf(fp, "\t\tcur_objects: %d\n", partition[i].cur_objects);
    if (verbose) {
      int total = 0;
      int n = 0;
      fprintf(fp, "\t\t[size_index block_size] by hash\n");
      for (int j = 0; j < partition_size; j++) {
        RamCacheEntry *e = partition[i].bucket[j].head;
        while (e) {
          printf("\t\t%9d %9d\n", e->data->_size_index, e->data->block_size());
          total += e->data->block_size();
          n++;
          e = e->hash_link.next;
        }
      }
      fprintf(fp, "\t\tTotal Size by hash: %d (%d average)\n", total, n ? total / n : 0);
      total = 0;
      n = 0;
      fprintf(fp, "\t\t[size_index block_size] by LRU\n");
      {
        RamCacheEntry *e = partition[i].lru.head;
        while (e) {
          printf("\t\t%9d %9d\n", e->data->_size_index, e->data->block_size());
          total += e->data->block_size();
          n++;
          e = e->lru_link.next;
        }
      }
      fprintf(fp, "\t\tTotal Size by LRU: %d (%d average)\n", total, n ? total / n : 0);
    }
  }
}
