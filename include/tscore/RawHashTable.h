/** @file

  C++ wrapper around libts hash tables

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

  @section details Details

  These C++ RawHashTables are a C++ wrapper around libts hash tables.
  They expose an interface very analogous to ink_hash_table, for better
  or for worse. See HashTable for a more C++-oriented hash table.

*/

#pragma once

#include "tscore/ink_apidefs.h"
#include "tscore/ink_hash_table.h"

//////////////////////////////////////////////////////////////////////////////
//
//      Constants and Type Definitions
//
//////////////////////////////////////////////////////////////////////////////

typedef enum {
  RawHashTable_KeyType_String = InkHashTableKeyType_String,
  RawHashTable_KeyType_Word   = InkHashTableKeyType_Word
} RawHashTable_KeyType;

typedef InkHashTableKey RawHashTable_Key;
typedef InkHashTableValue RawHashTable_Value;
typedef InkHashTableEntry RawHashTable_Binding;
typedef InkHashTableIteratorState RawHashTable_IteratorState;

//////////////////////////////////////////////////////////////////////////////
//
//      The RawHashTable Class
//
//////////////////////////////////////////////////////////////////////////////

class RawHashTable
{
private:
  InkHashTable *ht;
  RawHashTable_KeyType key_type;
  bool deallocate_values_on_destruct;

public:
  inkcoreapi RawHashTable(RawHashTable_KeyType key_type, bool deallocate_values_on_destruct = false);
  virtual ~RawHashTable();

  //
  // these are the simplest accessor functions
  //

  bool getValue(RawHashTable_Key key, RawHashTable_Value *value_ptr);
  void setValue(RawHashTable_Key key, RawHashTable_Value value_ptr);
  bool isBound(RawHashTable_Key key);
  bool unbindKey(RawHashTable_Key key);
  void replaceString(char *key, char *string);

  //
  // these functions allow you to manipulate the (key,value) bindings directly
  //

  RawHashTable_Binding *getCurrentBinding(RawHashTable_Key key);
  RawHashTable_Binding *getOrCreateBinding(RawHashTable_Key key, bool *was_new = nullptr);

  void setBindingValue(RawHashTable_Binding *binding, RawHashTable_Value value);
  RawHashTable_Key getKeyFromBinding(RawHashTable_Binding *binding);
  RawHashTable_Value getValueFromBinding(RawHashTable_Binding *binding);

  //
  // these functions allow you to iterate through RawHashTable bindings
  //

  RawHashTable_Binding *firstBinding(RawHashTable_IteratorState *state_ptr);
  RawHashTable_Binding *nextBinding(RawHashTable_IteratorState *state_ptr);
};

//////////////////////////////////////////////////////////////////////////////
//
//      Inline Methods
//
//////////////////////////////////////////////////////////////////////////////

/**
  This routine gets the value associated with key. If the key has a
  binding, the value is stored through value_ptr and true is returned. If
  the key DOES NOT have a binding, false is returned.

*/
inline bool
RawHashTable::getValue(RawHashTable_Key key, RawHashTable_Value *value_ptr)
{
  int is_bound;

  is_bound = ink_hash_table_lookup(ht, (InkHashTableKey)key, (InkHashTableValue *)value_ptr);
  return (is_bound ? true : false);
}

/**
  This routine sets the value associated with key to the value. If
  a value is previously bound to the key, the previous value is left
  dangling. The caller is responsible to freeing any previous binding
  values needing freeing before calling setValue.

  If the key has a binding, the value is stored through value_ptr and
  true is returned. If the key DOES NOT have a binding, false is returned.

*/
inline void
RawHashTable::setValue(RawHashTable_Key key, RawHashTable_Value value)
{
  ink_hash_table_insert(ht, (InkHashTableKey)key, (InkHashTableValue)value);
}

/**
  This routine sets the value associated with key to the value pointed to
  by value_ptr. If a value is previously bound to the key, the previous
  value is left dangling. The caller is responsible to freeing any
  previous value before setValue.

  If the key has a binding, the value is stored through value_ptr and
  true is returned. If the key DOES NOT have a binding, false is returned.

*/
inline bool
RawHashTable::isBound(RawHashTable_Key key)
{
  int status = ink_hash_table_isbound(ht, (InkHashTableKey)key);
  return (status ? true : false);
}

/**
  This routine removes any association for key from the hash table. If
  data was bound to key, the binding will be deleted, but the value will
  not be deallocated. The caller is responsible to freeing any previous
  value before unbindKey.

  @return true if the key was previously bound, false otherwise.

*/
inline bool
RawHashTable::unbindKey(RawHashTable_Key key)
{
  int status;

  status = ink_hash_table_delete(ht, (InkHashTableKey)key);
  return (status ? true : false);
}

/**
  This rather specialized routine binds a malloc-allocated string value
  to the key, freeing any previous value. The key must be a string,
  and the hash table must have been constructed as having key_type
  RawHashTable_KeyType_String.

*/
inline void
RawHashTable::replaceString(char *key, char *string)
{
  //    if (key_type != RawHashTable_KeyType_String)
  //    {
  //      throw BadKeyType();
  //    }

  ink_hash_table_replace_string(ht, key, string);
}

/**
  This function looks up a binding for key in the hash table, and returns
  a pointer to the binding data structure directly inside the hash table,
  or NULL if there is no binding.

*/
inline RawHashTable_Binding *
RawHashTable::getCurrentBinding(RawHashTable_Key key)
{
  InkHashTableEntry *he_ptr;

  he_ptr = ink_hash_table_lookup_entry(ht, (InkHashTableKey)key);
  return ((RawHashTable_Binding *)he_ptr);
}

/**
  This function looks up a binding for key in the hash table, creates
  a binding if one doesn't exist, and returns a pointer to the binding
  data structure directly inside the hash table.

  If was_new is not NULL, true is stored through was_new. If no binding
  previously existed, false is stored through was_new if a binding
  previously existed.

*/
inline RawHashTable_Binding *
RawHashTable::getOrCreateBinding(RawHashTable_Key key, bool *was_new)
{
  int _was_new;
  InkHashTableEntry *he_ptr;

  he_ptr   = ink_hash_table_get_entry(ht, (InkHashTableKey)key, &_was_new);
  *was_new = (_was_new ? true : false);
  return ((RawHashTable_Binding *)he_ptr);
}

/**
  This function looks up a binding for key in the hash table, creates
  a binding if one doesn't exist, and returns a pointer to the binding
  data structure directly inside the hash table.

  If was_new is not NULL, true is stored through was_new. If no binding
  previously existed, false is stored through was_new if a binding
  previously existed.

*/
inline void
RawHashTable::setBindingValue(RawHashTable_Binding *binding, RawHashTable_Value value)
{
  ink_hash_table_set_entry(ht, (InkHashTableEntry *)binding, (InkHashTableValue)value);
}

/**
  This function takes a binding and extracts the key.

*/
inline RawHashTable_Key
RawHashTable::getKeyFromBinding(RawHashTable_Binding *binding)
{
  InkHashTableKey ht_key;

  ht_key = ink_hash_table_entry_key(ht, (InkHashTableEntry *)binding);
  return ((RawHashTable_Key)ht_key);
}

/**
  This function takes a binding and extracts the value.

*/
inline RawHashTable_Value
RawHashTable::getValueFromBinding(RawHashTable_Binding *binding)
{
  InkHashTableValue ht_value;

  ht_value = ink_hash_table_entry_value(ht, (InkHashTableEntry *)binding);
  return ((RawHashTable_Value)ht_value);
}

/**
  This function takes a hash table, initializes an interator data
  structure to point to the first binding in the hash table, and returns
  the first binding, or NULL if there are none.

*/
inline RawHashTable_Binding *
RawHashTable::firstBinding(RawHashTable_IteratorState *state_ptr)
{
  InkHashTableEntry *he_ptr;

  he_ptr = ink_hash_table_iterator_first(ht, (InkHashTableIteratorState *)state_ptr);
  return ((RawHashTable_Binding *)he_ptr);
}

inline RawHashTable::RawHashTable(RawHashTable_KeyType akey_type, bool adeallocate_values_on_destruct)
{
  RawHashTable::key_type                      = akey_type;
  RawHashTable::deallocate_values_on_destruct = adeallocate_values_on_destruct;
  ht                                          = ink_hash_table_create((InkHashTableKeyType)key_type);
}

inline RawHashTable::~RawHashTable()
{
  if (deallocate_values_on_destruct) {
    ink_hash_table_destroy_and_free_values(ht);
  } else {
    ink_hash_table_destroy(ht);
  }
}

/**
  This function takes a hash table and a pointer to iterator state,
  and advances to the next binding in the hash table, if any. If there
  in a next binding, a pointer to the binding is returned, else NULL.

*/
inline RawHashTable_Binding *
RawHashTable::nextBinding(RawHashTable_IteratorState *state_ptr)
{
  InkHashTableEntry *he_ptr;

  he_ptr = ink_hash_table_iterator_next(ht, (InkHashTableIteratorState *)state_ptr);
  return ((RawHashTable_Binding *)he_ptr);
}

//////////////////////////////////////////////////////////////////////////////
//
//      The RawHashTableIter Class
//
//////////////////////////////////////////////////////////////////////////////

class RawHashTableIter
{
public:
  RawHashTableIter(RawHashTable &ht);
  ~RawHashTableIter();

  RawHashTable_Value &operator++();       // get next
  RawHashTable_Value &operator()() const; // get current
  operator const void *() const;          // is valid

  RawHashTable_Value &value() const; // get current value
  const char *key() const;           // get current key

private:
  RawHashTable &m_ht;
  RawHashTable_Binding *m_currentBinding;
  RawHashTable_IteratorState m_hashIterState;
};

//////////////////////////////////////////////////////////////////////////////
//
//      Inline Methods
//
//////////////////////////////////////////////////////////////////////////////

inline RawHashTable_Value &
RawHashTableIter::operator()() const
{
  return (m_currentBinding->clientData);
}

inline RawHashTable_Value &
RawHashTableIter::operator++()
{
  m_currentBinding = m_ht.nextBinding(&m_hashIterState);
  return (m_currentBinding->clientData);
}

inline RawHashTableIter::operator const void *() const
{
  return ((m_currentBinding != nullptr) ? this : nullptr);
}

inline RawHashTable_Value &
RawHashTableIter::value() const
{
  return (m_currentBinding->clientData);
}

inline const char *
RawHashTableIter::key() const
{
  return (m_currentBinding->key.string);
}

inline RawHashTableIter::RawHashTableIter(RawHashTable &ht) : m_ht(ht), m_currentBinding(nullptr)
{
  m_currentBinding = m_ht.firstBinding(&m_hashIterState);
  return;
}

inline RawHashTableIter::~RawHashTableIter()
{
  return;
}
