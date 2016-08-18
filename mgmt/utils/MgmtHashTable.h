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

/*
 *
 * MgmtHashTable.h
 *
 * $Date: 2003-06-01 18:38:21 $
 *
 *
 */

#ifndef _MGMT_HASH_TABLE_H
#define _MGMT_HASH_TABLE_H

#include "ts/ink_memory.h"
#include "ts/ink_hash_table.h"
#include "ts/ink_mutex.h"

class MgmtHashTable
{
public:
  MgmtHashTable(const char *name, bool free_on_delete, InkHashTableKeyType type)
  {
    ink_mutex_init(&mutex, name);
    destroy_and_free = free_on_delete;
    ht               = ink_hash_table_create(type);
  }

  ~MgmtHashTable()
  {
    ink_mutex_acquire(&mutex);
    if (destroy_and_free) {
      ink_hash_table_destroy_and_free_values(ht);
    } else {
      ink_hash_table_destroy(ht);
    }
    ink_mutex_release(&mutex);
  }

  int
  mgmt_hash_table_isbound(InkHashTableKey key)
  {
    int ret;

    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_isbound(ht, key);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_isbound */

  int
  mgmt_hash_table_lookup(const char *key, InkHashTableValue *value_ptr)
  {
    int ret;
    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_lookup(ht, key, value_ptr);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_lookup */

  int
  mgmt_hash_table_delete(InkHashTableKey key)
  {
    InkHashTableEntry *entry;
    int ret;
    ink_mutex_acquire(&mutex);
    if (destroy_and_free) {
      entry = ink_hash_table_lookup_entry(ht, key);
      if (entry != NULL) {
        InkHashTableValue value = ink_hash_table_entry_value(ht, entry);
        if (value != NULL) {
          ats_free(value);
        }
        ret = ink_hash_table_delete(ht, key);
      } else { // not found
        ret = 0;
      }
    } else {
      ret = ink_hash_table_delete(ht, key);
    }
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_delete */

  InkHashTableEntry *
  mgmt_hash_table_lookup_entry(InkHashTableKey key)
  {
    InkHashTableEntry *ret;
    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_lookup_entry(ht, key);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_lookup_entry */

  InkHashTableEntry *
  mgmt_hash_table_get_entry(InkHashTableKey key, int *new_value)
  {
    InkHashTableEntry *ret;
    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_get_entry(ht, key, new_value);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_get_entry */

  void
  mgmt_hash_table_insert(const char *key, InkHashTableValue value)
  {
    ink_mutex_acquire(&mutex);
    ink_hash_table_insert(ht, key, value);
    ink_mutex_release(&mutex);
    return;
  } /* End MgmtHashTable::mgmt_hash_table_insert */

  InkHashTableEntry *
  mgmt_hash_table_iterator_first(InkHashTableIteratorState *state_ptr)
  {
    InkHashTableEntry *ret;
    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_iterator_first(ht, state_ptr);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_iterator_first */

  InkHashTableEntry *
  mgmt_hash_table_iterator_next(InkHashTableIteratorState *state_ptr)
  {
    InkHashTableEntry *ret;
    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_iterator_next(ht, state_ptr);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_iterator_next */

  InkHashTableKey
  mgmt_hash_table_entry_key(InkHashTableEntry *entry_ptr)
  {
    InkHashTableKey ret;
    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_entry_key(ht, entry_ptr);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_entry_key */

  InkHashTableValue
  mgmt_hash_table_entry_value(InkHashTableEntry *entry_ptr)
  {
    InkHashTableValue ret;
    ink_mutex_acquire(&mutex);
    ret = ink_hash_table_entry_value(ht, entry_ptr);
    ink_mutex_release(&mutex);
    return ret;
  } /* End MgmtHashTable::mgmt_hash_table_entry_value */

  void
  mgmt_hash_table_dump_strings()
  {
    ink_mutex_acquire(&mutex);
    ink_hash_table_dump_strings(ht);
    ink_mutex_release(&mutex);
    return;
  } /* End MgmtHashTable::mgmt_hash_Table_dump_strings */

private:
  bool destroy_and_free;

  InkHashTable *ht;
  ink_mutex mutex;

}; /* End class MgmtHashTable */

#endif /* _MGMT_HASH_TABLE_H */
