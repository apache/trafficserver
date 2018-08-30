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

  ink_hash_table.c

  This file implements hash tables.  This allows us to provide alternative
  implementations of hash tables.

 ****************************************************************************/

#include "tscore/ink_error.h"
#include "tscore/ink_hash_table.h"
#include "tscore/ink_memory.h"

/*===========================================================================*

  This is the Tcl implementation of InkHashTable

 *===========================================================================*/

/*---------------------------------------------------------------------------*

  InkHashTable *ink_hash_table_create(InkHashTableKeyType key_type)

  This routine allocates an initializes an empty InkHashTable, and returns a
  pointer to the new table.  The <key_type> argument indicates whether keys
  are represented as strings, or as words.  Legal values are
  InkHashTableKeyType_String and InkHashTableKeyType_Word.

 *---------------------------------------------------------------------------*/

InkHashTable *
ink_hash_table_create(InkHashTableKeyType key_type)
{
  InkHashTable *ht_ptr;
  Tcl_HashTable *tcl_ht_ptr;
  int tcl_key_type;

  tcl_ht_ptr = (Tcl_HashTable *)ats_malloc(sizeof(Tcl_HashTable));

  if (key_type == InkHashTableKeyType_String) {
    tcl_key_type = TCL_STRING_KEYS;
  } else if (key_type == InkHashTableKeyType_Word) {
    tcl_key_type = TCL_ONE_WORD_KEYS;
  } else {
    ink_fatal("ink_hash_table_create: bad key_type %d", key_type);
  }

  Tcl_InitHashTable(tcl_ht_ptr, tcl_key_type);

  ht_ptr = (InkHashTable *)tcl_ht_ptr;
  return (ht_ptr);
} /* End ink_hash_table_create */

/*---------------------------------------------------------------------------*

  void ink_hash_table_destroy(InkHashTable *ht_ptr)

  This routine takes a hash table <ht_ptr>, and frees its storage.

 *---------------------------------------------------------------------------*/

InkHashTable *
ink_hash_table_destroy(InkHashTable *ht_ptr)
{
  Tcl_HashTable *tcl_ht_ptr;

  tcl_ht_ptr = (Tcl_HashTable *)ht_ptr;
  Tcl_DeleteHashTable(tcl_ht_ptr);
  ats_free(tcl_ht_ptr);
  return (InkHashTable *)nullptr;
} /* End ink_hash_table_destroy */

/*---------------------------------------------------------------------------*

  void ink_hash_table_destroy_and_free_values(InkHashTable *ht_ptr)

  This routine takes a hash table <ht_ptr>, and frees its storage, after
  first calling ink_free on all the values.  You better darn well make sure the
  values have been dynamically allocated.

 *---------------------------------------------------------------------------*/

static int
_ink_hash_table_free_entry_value(InkHashTable *ht_ptr, InkHashTableEntry *e)
{
  InkHashTableValue value;

  value = ink_hash_table_entry_value(ht_ptr, e);
  if (value != nullptr) {
    ats_free(value);
  }

  return (0);
} /* End _ink_hash_table_free_entry_value */

InkHashTable *
ink_hash_table_destroy_and_free_values(InkHashTable *ht_ptr)
{
  ink_hash_table_map(ht_ptr, _ink_hash_table_free_entry_value);
  ink_hash_table_destroy(ht_ptr);
  return (InkHashTable *)nullptr;
} /* End ink_hash_table_destroy_and_free_values */

/*---------------------------------------------------------------------------*

  int ink_hash_table_isbound(InkHashTable *ht_ptr, InkHashTableKey key)

  This routine takes a hash table <ht_ptr>, a key <key>, and returns 1
  if the value <key> is bound in the hash table, 0 otherwise.

 *---------------------------------------------------------------------------*/

int
ink_hash_table_isbound(InkHashTable *ht_ptr, const char *key)
{
  InkHashTableEntry *he_ptr;

  he_ptr = ink_hash_table_lookup_entry(ht_ptr, key);
  return ((he_ptr == nullptr) ? 0 : 1);
} /* End ink_hash_table_isbound */

/*---------------------------------------------------------------------------*
  int ink_hash_table_lookup(InkHashTable *ht_ptr,
                            InkHashTableKey key,
                            InkHashTableValue *value_ptr)

  This routine takes a hash table <ht_ptr>, a key <key>, and stores the
  value bound to the key by reference through <value_ptr>.  If no binding is
  found, 0 is returned, else 1 is returned.

 *---------------------------------------------------------------------------*/

int
ink_hash_table_lookup(InkHashTable *ht_ptr, const char *key, InkHashTableValue *value_ptr)
{
  InkHashTableEntry *he_ptr;
  InkHashTableValue value;

  he_ptr = ink_hash_table_lookup_entry(ht_ptr, key);
  if (he_ptr == nullptr) {
    return (0);
  }

  value      = ink_hash_table_entry_value(ht_ptr, he_ptr);
  *value_ptr = value;
  return (1);
} /* End ink_hash_table_lookup */

/*---------------------------------------------------------------------------*

  int ink_hash_table_delete(InkHashTable *ht_ptr, InkHashTableKey key)

  This routine takes a hash table <ht_ptr> and a key <key>, and deletes the
  binding for the <key> in the hash table if it exists.  This routine
  returns 1 if the key existed, else 0.

 *---------------------------------------------------------------------------*/

int
ink_hash_table_delete(InkHashTable *ht_ptr, const char *key)
{
  char *tcl_key;
  Tcl_HashTable *tcl_ht_ptr;
  Tcl_HashEntry *tcl_he_ptr;

  tcl_key    = (char *)key;
  tcl_ht_ptr = (Tcl_HashTable *)ht_ptr;
  tcl_he_ptr = Tcl_FindHashEntry(tcl_ht_ptr, tcl_key);

  if (!tcl_he_ptr) {
    return (0);
  }
  Tcl_DeleteHashEntry(tcl_he_ptr);

  return (1);
} /* End ink_hash_table_delete */

/*---------------------------------------------------------------------------*

  InkHashTableEntry *ink_hash_table_lookup_entry(InkHashTable *ht_ptr,
                                                 InkHashTableKey key)

  This routine takes a hash table <ht_ptr> and a key <key>, and returns the
  entry matching the key, or nullptr otherwise.

 *---------------------------------------------------------------------------*/

InkHashTableEntry *
ink_hash_table_lookup_entry(InkHashTable *ht_ptr, const char *key)
{
  Tcl_HashTable *tcl_ht_ptr;
  Tcl_HashEntry *tcl_he_ptr;
  InkHashTableEntry *he_ptr;

  tcl_ht_ptr = (Tcl_HashTable *)ht_ptr;
  tcl_he_ptr = Tcl_FindHashEntry(tcl_ht_ptr, key);
  he_ptr     = (InkHashTableEntry *)tcl_he_ptr;

  return (he_ptr);
} /* End ink_hash_table_lookup_entry */

/*---------------------------------------------------------------------------*

  InkHashTableEntry *ink_hash_table_get_entry(InkHashTable *ht_ptr,
                                              InkHashTableKey key,
                                              int *new_value)

  This routine takes a hash table <ht_ptr> and a key <key>, and returns the
  entry matching the key, or creates, binds, and returns a new entry.
  If the binding already existed, *new is set to 0, else 1.

 *---------------------------------------------------------------------------*/

InkHashTableEntry *
ink_hash_table_get_entry(InkHashTable *ht_ptr, const char *key, int *new_value)
{
  Tcl_HashTable *tcl_ht_ptr;
  Tcl_HashEntry *tcl_he_ptr;

  tcl_ht_ptr = (Tcl_HashTable *)ht_ptr;
  tcl_he_ptr = Tcl_CreateHashEntry(tcl_ht_ptr, key, new_value);

  if (tcl_he_ptr == nullptr) {
    ink_fatal("%s: Tcl_CreateHashEntry returned nullptr", "ink_hash_table_get_entry");
  }

  return ((InkHashTableEntry *)tcl_he_ptr);
} /* End ink_hash_table_get_entry */

/*---------------------------------------------------------------------------*

  void ink_hash_table_set_entry(InkHashTable *ht_ptr,
                                InkHashTableEntry *he_ptr,
                                InkHashTableValue value)

  This routine takes a hash table <ht_ptr>, a hash table entry <he_ptr>,
  and changes the value field of the entry to <value>.

 *---------------------------------------------------------------------------*/

void
ink_hash_table_set_entry(InkHashTable *ht_ptr, InkHashTableEntry *he_ptr, InkHashTableValue value)
{
  (void)ht_ptr;
  ClientData tcl_value;
  Tcl_HashEntry *tcl_he_ptr;

  tcl_value  = (ClientData)value;
  tcl_he_ptr = (Tcl_HashEntry *)he_ptr;
  Tcl_SetHashValue(tcl_he_ptr, tcl_value);
} /* End ink_hash_table_set_entry */

/*---------------------------------------------------------------------------*

  void ink_hash_table_insert(InkHashTable *ht_ptr,
                             InkHashTableKey key,
                             InkHashTableValue value)

  This routine takes a hash table <ht_ptr>, a key <key>, and binds the value
  <value> to the key, replacing any previous binding, if any.

 *---------------------------------------------------------------------------*/

void
ink_hash_table_insert(InkHashTable *ht_ptr, const char *key, InkHashTableValue value)
{
  int new_value;
  InkHashTableEntry *he_ptr;

  he_ptr = ink_hash_table_get_entry(ht_ptr, key, &new_value);
  ink_hash_table_set_entry(ht_ptr, he_ptr, value);
} /* End ink_hash_table_insert */

/*---------------------------------------------------------------------------*

  void ink_hash_table_map(InkHashTable *ht_ptr, InkHashTableEntryFunction map)

  This routine takes a hash table <ht_ptr> and a function pointer <map>, and
  applies the function <map> to each entry in the hash table.  The function
  <map> should return 0 normally, otherwise the iteration will stop.

 *---------------------------------------------------------------------------*/

void
ink_hash_table_map(InkHashTable *ht_ptr, InkHashTableEntryFunction map)
{
  int retcode;
  InkHashTableEntry *e;
  InkHashTableIteratorState state;

  for (e = ink_hash_table_iterator_first(ht_ptr, &state); e != nullptr; e = ink_hash_table_iterator_next(ht_ptr, &state)) {
    retcode = (*map)(ht_ptr, e);
    if (retcode != 0) {
      break;
    }
  }
} /* End ink_hash_table_map */

/*---------------------------------------------------------------------------*

  InkHashTableKey ink_hash_table_entry_key(InkHashTable *ht_ptr,
                                           InkHashTableEntry *entry_ptr)

  This routine takes a hash table <ht_ptr> and a pointer to a hash table
  entry <entry_ptr>, and returns the key portion of the entry.

 *---------------------------------------------------------------------------*/

InkHashTableKey
ink_hash_table_entry_key(InkHashTable *ht_ptr, InkHashTableEntry *entry_ptr)
{
  char *tcl_key;

  tcl_key = (char *)Tcl_GetHashKey((Tcl_HashTable *)ht_ptr, (Tcl_HashEntry *)entry_ptr);
  return ((InkHashTableKey)tcl_key);
} /* End ink_hash_table_entry_key */

/*---------------------------------------------------------------------------*

  InkHashTableValue ink_hash_table_entry_value(InkHashTable *ht_ptr,
                                               InkHashTableEntry *entry_ptr)

  This routine takes a hash table <ht_ptr> and a pointer to a hash table
  entry <entry_ptr>, and returns the value portion of the entry.

 *---------------------------------------------------------------------------*/

InkHashTableValue
ink_hash_table_entry_value(InkHashTable *ht_ptr, InkHashTableEntry *entry_ptr)
{
  (void)ht_ptr;
  ClientData tcl_value;

  tcl_value = Tcl_GetHashValue((Tcl_HashEntry *)entry_ptr);
  return ((InkHashTableValue)tcl_value);
} /* End ink_hash_table_entry_value */

/*---------------------------------------------------------------------------*

  void ink_hash_table_dump_strings(InkHashTable *ht_ptr)

  This routine takes a hash table of string values, and dumps the keys and
  string values to stdout.  It is the caller's responsibility to ensure that
  both the key and the value are NUL terminated strings.

 *---------------------------------------------------------------------------*/

static int
DumpStringEntry(InkHashTable *ht_ptr, InkHashTableEntry *e)
{
  InkHashTableKey key;
  InkHashTableValue value;

  key   = ink_hash_table_entry_key(ht_ptr, e);
  value = ink_hash_table_entry_value(ht_ptr, e);

  fprintf(stderr, "key = '%s', value = '%s'\n", (char *)key, (char *)value);

  return (0);
}

void
ink_hash_table_dump_strings(InkHashTable *ht_ptr)
{
  ink_hash_table_map(ht_ptr, DumpStringEntry);
} /* End ink_hash_table_dump_strings */

/*---------------------------------------------------------------------------*

  void ink_hash_table_replace_string(InkHashTable *ht_ptr,
                                     char *string_key, char *string_value)

  This conveninece routine is intended for hash tables with keys of type
  InkHashTableKeyType_String, and values being dynamically allocated strings.
  This routine binds <string_key> to a copy of <string_value>, and any
  previous bound value is deallocated.

 *---------------------------------------------------------------------------*/

void
ink_hash_table_replace_string(InkHashTable *ht_ptr, char *string_key, char *string_value)
{
  int new_value;
  char *old_str;
  InkHashTableEntry *he_ptr;

  /*
   * The following line will flag a type-conversion warning on the
   * DEC Alpha, but that message can be ignored, since we're
   * still dealing with pointers, and we aren't loosing any bits.
   */

  he_ptr = ink_hash_table_get_entry(ht_ptr, (InkHashTableKey)string_key, &new_value);
  if (new_value == 0) {
    old_str = (char *)ink_hash_table_entry_value(ht_ptr, he_ptr);
    if (old_str) {
      ats_free(old_str);
    }
  }

  ink_hash_table_set_entry(ht_ptr, he_ptr, (InkHashTableValue)(ats_strdup(string_value)));
} /* End ink_hash_table_replace_string */
