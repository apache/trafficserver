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

  MT_hashtable.h

  Multithread Safe Hash table implementation


 ****************************************************************************/
#pragma once

#define MT_HASHTABLE_PARTITION_BITS 6
#define MT_HASHTABLE_PARTITIONS (1 << MT_HASHTABLE_PARTITION_BITS)
#define MT_HASHTABLE_PARTITION_MASK (MT_HASHTABLE_PARTITIONS - 1)
#define MT_HASHTABLE_MAX_CHAIN_AVG_LEN 4
template <class key_t, class data_t> struct HashTableEntry {
  key_t key;
  data_t data;
  HashTableEntry *next;

  static HashTableEntry *
  alloc()
  {
    return (HashTableEntry *)ats_malloc(sizeof(HashTableEntry));
  }

  static void
  free(HashTableEntry *entry)
  {
    ats_free(entry);
  }
};

/*
struct MT_ListEntry{
  MT_ListEntry():next(NULL),prev(NULL){}
  MT_ListEntry* next;
  MT_ListEntry* prev;
};

#define INIT_CHAIN_HEAD(h) {(h)->next = (h)->prev = (h);}
#define APPEND_TO_CHAIN(h, p) {(p)->next = (h)->next; (h)->next->prev = (p); (p)->prev = (h); (h)->next = (p);}
#define REMOVE_FROM_CHAIN(p) {(p)->next->prev = (p)->prev; (p)->prev->next = (p)->next; (p)->prev = (p)->next = NULL;}
#define GET_OBJ_PTR(p, type, offset) ((type*)((char*)(p) - offset))
*/

template <class key_t, class data_t> class HashTableIteratorState
{
public:
  HashTableIteratorState() : ppcur(NULL) {}
  int cur_buck = -1;
  HashTableEntry<key_t, data_t> **ppcur;
};

template <class key_t, class data_t> class IMTHashTable
{
public:
  IMTHashTable(int size, bool (*gc_func)(data_t) = NULL, void (*pre_gc_func)() = nullptr)
  {
    m_gc_func     = gc_func;
    m_pre_gc_func = pre_gc_func;
    bucket_num    = size;
    cur_size      = 0;
    buckets       = new HashTableEntry<key_t, data_t> *[bucket_num];
    memset(buckets, 0, bucket_num * sizeof(HashTableEntry<key_t, data_t> *));
  }
  ~IMTHashTable() { reset(); }
  int
  getBucketNum()
  {
    return bucket_num;
  }
  int
  getCurSize()
  {
    return cur_size;
  }

  int
  bucket_id(key_t key, int a_bucket_num)
  {
    return (int)(((key >> MT_HASHTABLE_PARTITION_BITS) ^ key) % a_bucket_num);
  }

  int
  bucket_id(key_t key)
  {
    return bucket_id(key, bucket_num);
  }

  void
  reset()
  {
    HashTableEntry<key_t, data_t> *tmp;
    for (int i = 0; i < bucket_num; i++) {
      tmp = buckets[i];
      while (tmp) {
        buckets[i] = tmp->next;
        HashTableEntry<key_t, data_t>::free(tmp);
        tmp = buckets[i];
      }
    }
    delete[] buckets;
    buckets = NULL;
  }

  data_t insert_entry(key_t key, data_t data);
  data_t remove_entry(key_t key);
  data_t lookup_entry(key_t key);

  data_t first_entry(int bucket_id, HashTableIteratorState<key_t, data_t> *s);
  static data_t next_entry(HashTableIteratorState<key_t, data_t> *s);
  static data_t cur_entry(HashTableIteratorState<key_t, data_t> *s);
  data_t remove_entry(HashTableIteratorState<key_t, data_t> *s);

  void
  GC()
  {
    if (m_gc_func == NULL) {
      return;
    }
    if (m_pre_gc_func) {
      m_pre_gc_func();
    }
    for (int i = 0; i < bucket_num; i++) {
      HashTableEntry<key_t, data_t> *cur  = buckets[i];
      HashTableEntry<key_t, data_t> *prev = NULL;
      HashTableEntry<key_t, data_t> *next = NULL;
      while (cur != NULL) {
        next = cur->next;
        if (m_gc_func(cur->data)) {
          if (prev != NULL) {
            prev->next = next;
          } else {
            buckets[i] = next;
          }
          ats_free(cur);
          cur_size--;
        } else {
          prev = cur;
        }
        cur = next;
      }
    }
  }

  void
  resize(int size)
  {
    int new_bucket_num                          = size;
    HashTableEntry<key_t, data_t> **new_buckets = new HashTableEntry<key_t, data_t> *[new_bucket_num];
    memset(new_buckets, 0, new_bucket_num * sizeof(HashTableEntry<key_t, data_t> *));

    for (int i = 0; i < bucket_num; i++) {
      HashTableEntry<key_t, data_t> *cur  = buckets[i];
      HashTableEntry<key_t, data_t> *next = NULL;
      while (cur != NULL) {
        next                = cur->next;
        int new_id          = bucket_id(cur->key, new_bucket_num);
        cur->next           = new_buckets[new_id];
        new_buckets[new_id] = cur;
        cur                 = next;
      }
      buckets[i] = NULL;
    }
    delete[] buckets;
    buckets    = new_buckets;
    bucket_num = new_bucket_num;
  }

private:
  HashTableEntry<key_t, data_t> **buckets;
  int cur_size;
  int bucket_num;
  bool (*m_gc_func)(data_t);
  void (*m_pre_gc_func)();

private:
  IMTHashTable();
  IMTHashTable(IMTHashTable &);
};

/*
 * we can use ClassAllocator here if the malloc performance becomes a problem
 */

template <class key_t, class data_t>
inline data_t
IMTHashTable<key_t, data_t>::insert_entry(key_t key, data_t data)
{
  int id                             = bucket_id(key);
  HashTableEntry<key_t, data_t> *cur = buckets[id];
  while (cur != NULL && cur->key != key) {
    cur = cur->next;
  }
  if (cur != NULL) {
    if (data == cur->data) {
      return (data_t)0;
    } else {
      data_t tmp = cur->data;
      cur->data  = data;
      // potential memory leak, need to check the return value by the caller
      return tmp;
    }
  }

  HashTableEntry<key_t, data_t> *newEntry = HashTableEntry<key_t, data_t>::alloc();
  newEntry->key                           = key;
  newEntry->data                          = data;
  newEntry->next                          = buckets[id];
  buckets[id]                             = newEntry;
  cur_size++;
  if (cur_size / bucket_num > MT_HASHTABLE_MAX_CHAIN_AVG_LEN) {
    GC();
    if (cur_size / bucket_num > MT_HASHTABLE_MAX_CHAIN_AVG_LEN) {
      resize(bucket_num * 2);
    }
  }
  return (data_t)0;
}

template <class key_t, class data_t>
inline data_t
IMTHashTable<key_t, data_t>::remove_entry(key_t key)
{
  int id                              = bucket_id(key);
  data_t ret                          = (data_t)0;
  HashTableEntry<key_t, data_t> *cur  = buckets[id];
  HashTableEntry<key_t, data_t> *prev = NULL;
  while (cur != NULL && cur->key != key) {
    prev = cur;
    cur  = cur->next;
  }
  if (cur != NULL) {
    if (prev != NULL) {
      prev->next = cur->next;
    } else {
      buckets[id] = cur->next;
    }
    ret = cur->data;
    HashTableEntry<key_t, data_t>::free(cur);
    cur_size--;
  }

  return ret;
}

template <class key_t, class data_t>
inline data_t
IMTHashTable<key_t, data_t>::lookup_entry(key_t key)
{
  int id                             = bucket_id(key);
  data_t ret                         = (data_t)0;
  HashTableEntry<key_t, data_t> *cur = buckets[id];
  while (cur != NULL && cur->key != key) {
    cur = cur->next;
  }
  if (cur != NULL) {
    ret = cur->data;
  }
  return ret;
}

template <class key_t, class data_t>
inline data_t
IMTHashTable<key_t, data_t>::first_entry(int bucket_id, HashTableIteratorState<key_t, data_t> *s)
{
  s->cur_buck = bucket_id;
  s->ppcur    = &(buckets[bucket_id]);
  if (*(s->ppcur) != NULL) {
    return (*(s->ppcur))->data;
  }
  return (data_t)0;
}

template <class key_t, class data_t>
inline data_t
IMTHashTable<key_t, data_t>::next_entry(HashTableIteratorState<key_t, data_t> *s)
{
  if ((*(s->ppcur)) != NULL) {
    s->ppcur = &((*(s->ppcur))->next);
    if (*(s->ppcur) != NULL) {
      return (*(s->ppcur))->data;
    }
  }
  return (data_t)0;
}

template <class key_t, class data_t>
inline data_t
IMTHashTable<key_t, data_t>::cur_entry(HashTableIteratorState<key_t, data_t> *s)
{
  if (*(s->ppcur) == NULL) {
    return (data_t)0;
  }
  return (*(s->ppcur))->data;
}

template <class key_t, class data_t>
inline data_t
IMTHashTable<key_t, data_t>::remove_entry(HashTableIteratorState<key_t, data_t> *s)
{
  data_t data                           = (data_t)0;
  HashTableEntry<key_t, data_t> *pEntry = *(s->ppcur);
  if (pEntry != NULL) {
    data          = pEntry->data;
    (*(s->ppcur)) = pEntry->next;
    HashTableEntry<key_t, data_t>::free(pEntry);
    cur_size--;
  }
  return data;
}

template <class key_t, class data_t> class MTHashTable
{
public:
  MTHashTable(int size, bool (*gc_func)(data_t) = NULL, void (*pre_gc_func)() = nullptr)
  {
    for (int i = 0; i < MT_HASHTABLE_PARTITIONS; i++) {
      locks[i]      = new_ProxyMutex();
      hashTables[i] = new IMTHashTable<key_t, data_t>(size, gc_func, pre_gc_func);
      // INIT_CHAIN_HEAD(&chain_heads[i]);
      // last_GC_time[i] = 0;
    }
    //    cur_items = 0;
  }
  ~MTHashTable()
  {
    for (int i = 0; i < MT_HASHTABLE_PARTITIONS; i++) {
      locks[i] = NULL;
      delete hashTables[i];
    }
  }

  Ptr<ProxyMutex>
  lock_for_key(key_t key)
  {
    return locks[part_num(key)];
  }

  int
  getSize()
  {
    return MT_HASHTABLE_PARTITIONS;
  }
  int
  part_num(key_t key)
  {
    return (int)(key & MT_HASHTABLE_PARTITION_MASK);
  }
  data_t
  insert_entry(key_t key, data_t data)
  {
    // ink_atomic_increment(&cur_items, 1);
    return hashTables[part_num(key)]->insert_entry(key, data);
  }
  data_t
  remove_entry(key_t key)
  {
    // ink_atomic_increment(&cur_items, -1);
    return hashTables[part_num(key)]->remove_entry(key);
  }
  data_t
  lookup_entry(key_t key)
  {
    return hashTables[part_num(key)]->lookup_entry(key);
  }

  data_t
  first_entry(int part_id, HashTableIteratorState<key_t, data_t> *s)
  {
    data_t ret = (data_t)0;
    for (int i = 0; i < hashTables[part_id]->getBucketNum(); i++) {
      ret = hashTables[part_id]->first_entry(i, s);
      if (ret != (data_t)0) {
        return ret;
      }
    }
    return (data_t)0;
  }

  data_t
  cur_entry(int part_id, HashTableIteratorState<key_t, data_t> *s)
  {
    data_t data = IMTHashTable<key_t, data_t>::cur_entry(s);
    if (!data) {
      data = next_entry(part_id, s);
    }
    return data;
  };
  data_t
  next_entry(int part_id, HashTableIteratorState<key_t, data_t> *s)
  {
    data_t ret = IMTHashTable<key_t, data_t>::next_entry(s);
    if (ret != (data_t)0) {
      return ret;
    }
    for (int i = s->cur_buck + 1; i < hashTables[part_id]->getBucketNum(); i++) {
      ret = hashTables[part_id]->first_entry(i, s);
      if (ret != (data_t)0) {
        return ret;
      }
    }
    return (data_t)0;
  }
  data_t
  remove_entry(int part_id, HashTableIteratorState<key_t, data_t> *s)
  {
    // ink_atomic_increment(&cur_items, -1);
    return hashTables[part_id]->remove_entry(s);
  }

private:
  IMTHashTable<key_t, data_t> *hashTables[MT_HASHTABLE_PARTITIONS];
  Ptr<ProxyMutex> locks[MT_HASHTABLE_PARTITIONS];
  // MT_ListEntry chain_heads[MT_HASHTABLE_PARTITIONS];
  // int last_GC_time[MT_HASHTABLE_PARTITIONS];
  // int32_t cur_items;
};
