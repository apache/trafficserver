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

  ink_hash_table.h

  This file implements hash tables.  This allows us to provide alternative
  implementations of hash tables.

 ****************************************************************************/

#ifndef _ink_hash_table_h_
#define _ink_hash_table_h_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include "ts/ink_apidefs.h"

/*===========================================================================*

  This is the Tcl implementation of InkHashTable

 *===========================================================================*/

#include <tcl.h>

typedef Tcl_HashTable InkHashTable;
typedef Tcl_HashEntry InkHashTableEntry;
typedef char *InkHashTableKey;
typedef ClientData InkHashTableValue;
typedef Tcl_HashSearch InkHashTableIteratorState;

typedef int (*InkHashTableEntryFunction)(InkHashTable *ht, InkHashTableEntry *entry);

/* Types of InkHashTable Keys */

typedef enum {
  InkHashTableKeyType_String,
  InkHashTableKeyType_Word,
} InkHashTableKeyType;

/*===========================================================================*

                            Function Prototypes

 *===========================================================================*/

InkHashTable *ink_hash_table_create(InkHashTableKeyType key_type);
InkHashTable *ink_hash_table_destroy(InkHashTable *ht_ptr);
InkHashTable *ink_hash_table_destroy_and_free_values(InkHashTable *ht_ptr);
inkcoreapi int ink_hash_table_isbound(InkHashTable *ht_ptr, const char *key);
inkcoreapi int ink_hash_table_lookup(InkHashTable *ht_ptr, const char *key, InkHashTableValue *value_ptr);
inkcoreapi int ink_hash_table_delete(InkHashTable *ht_ptr, const char *key);
InkHashTableEntry *ink_hash_table_lookup_entry(InkHashTable *ht_ptr, const char *key);
InkHashTableEntry *ink_hash_table_get_entry(InkHashTable *ht_ptr, const char *key, int *new_value);
void ink_hash_table_set_entry(InkHashTable *ht_ptr, InkHashTableEntry *he_ptr, InkHashTableValue value);
inkcoreapi void ink_hash_table_insert(InkHashTable *ht_ptr, const char *key, InkHashTableValue value);
void ink_hash_table_map(InkHashTable *ht_ptr, InkHashTableEntryFunction map);
InkHashTableKey ink_hash_table_entry_key(InkHashTable *ht_ptr, InkHashTableEntry *entry_ptr);
inkcoreapi InkHashTableValue ink_hash_table_entry_value(InkHashTable *ht_ptr, InkHashTableEntry *entry_ptr);
void ink_hash_table_dump_strings(InkHashTable *ht_ptr);
void ink_hash_table_replace_string(InkHashTable *ht_ptr, InkHashTableKey key, char *str);

/* inline functions declarations */

/*---------------------------------------------------------------------------*

  InkHashTableEntry *ink_hash_table_iterator_first
        (InkHashTable *ht_ptr, InkHashTableIteratorState *state_ptr)

  This routine takes a hash table <ht_ptr>, creates some iterator state
  stored through <state_ptr>, and returns a pointer to the first hash table
  entry.  The iterator state is used by InkHashTableIteratorNext() to proceed
  through the rest of the entries.

 *---------------------------------------------------------------------------*/

static inline InkHashTableEntry *
ink_hash_table_iterator_first(InkHashTable *ht_ptr, InkHashTableIteratorState *state_ptr)
{
  Tcl_HashTable *tcl_ht_ptr;
  Tcl_HashSearch *tcl_search_state_ptr;
  Tcl_HashEntry *tcl_he_ptr;
  InkHashTableEntry *he_ptr;

  tcl_ht_ptr           = (Tcl_HashTable *)ht_ptr;
  tcl_search_state_ptr = (Tcl_HashSearch *)state_ptr;

  tcl_he_ptr = Tcl_FirstHashEntry(tcl_ht_ptr, tcl_search_state_ptr);
  he_ptr     = (InkHashTableEntry *)tcl_he_ptr;

  return (he_ptr);
} /* End ink_hash_table_iterator_first */

/*---------------------------------------------------------------------------*

  InkHashTableEntry *ink_hash_table_iterator_next(InkHashTable *ht_ptr,
                                                  InkHashTableIteratorState *state_ptr)

  This routine takes a hash table <ht_ptr> and a pointer to iterator state
  initialized by a previous call to InkHashTableIteratorFirst(), and returns
  a pointer to the next InkHashTableEntry, or NULL if no entries remain.

 *---------------------------------------------------------------------------*/

static inline InkHashTableEntry *
ink_hash_table_iterator_next(InkHashTable *ht_ptr, InkHashTableIteratorState *state_ptr)
{
  (void)ht_ptr;
  Tcl_HashSearch *tcl_search_state_ptr;
  Tcl_HashEntry *tcl_he_ptr;
  InkHashTableEntry *he_ptr;

  tcl_search_state_ptr = (Tcl_HashSearch *)state_ptr;

  tcl_he_ptr = Tcl_NextHashEntry(tcl_search_state_ptr);
  he_ptr     = (InkHashTableEntry *)tcl_he_ptr;

  return (he_ptr);
} /* End ink_hash_table_iterator_next */

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* #ifndef _ink_hash_table_h_ */
