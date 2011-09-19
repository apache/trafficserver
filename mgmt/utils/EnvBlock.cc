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

/***************************************/
#include "libts.h"
#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

#include "EnvBlock.h"
#include "string.h"


EnvBlock::EnvBlock(void)
{
  table = ink_hash_table_create(InkHashTableKeyType_String);
  arr_len = str_len = 0;
  env_arr = NULL;
  env_str = NULL;
}

EnvBlock::~EnvBlock(void)
{
  ink_hash_table_destroy(table);

  if (env_arr) {
    for (int i = 0; i < arr_len; i++) {
      if (env_arr[i] != NULL) {
        delete[]env_arr[i];
      }
    }
    delete[]env_arr;
  }

  if (env_str) {
    delete[]env_str;
  }
}

void
EnvBlock::setVar(const char *name, const char *value)
{
  if (name && value) {
    ink_hash_table_insert(table, (InkHashTableKey) name, (InkHashTableValue) value);
    arr_len++;
    str_len += strlen(name) + 1;
    str_len += strlen(value) + 1;
  }
}

char **
EnvBlock::toStringArray(void)
{
  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry;
  const char *name;
  const char *value;
  env_arr = new char *[arr_len + 1];
  int i = 0;

  ht_entry = ink_hash_table_iterator_first(table, &ht_iter);

  for (; ht_entry != NULL && i < arr_len; i++) {
    name = (const char *) ink_hash_table_entry_key(table, ht_entry);
    value = (const char *) ink_hash_table_entry_value(table, ht_entry);
    env_arr[i] = new char[strlen(name) + strlen(value) + 2];
    snprintf(env_arr[i], arr_len + 1, "%s=%s", name, value);
    ht_entry = ink_hash_table_iterator_next(table, &ht_iter);
  }
  env_arr[i] = NULL;

  return env_arr;
}

char *
EnvBlock::toString(void)
{
  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry;
  const char *name;
  const char *value;
  env_str = new char[str_len + 1];
  int i = 0;

  ht_entry = ink_hash_table_iterator_first(table, &ht_iter);

  while (ht_entry != NULL && i < str_len) {
    name = (const char *) ink_hash_table_entry_key(table, ht_entry);
    value = (const char *) ink_hash_table_entry_value(table, ht_entry);
    ink_strlcpy(env_str + i, name, str_len - i);
    i += strlen(name);
    ink_strlcpy(env_str + i, "=", str_len - i);
    i++;
    ink_strlcpy(env_str + i, value - i, str_len - i);
    i += strlen(value) + 1;
    ht_entry = ink_hash_table_iterator_next(table, &ht_iter);
  }
  env_str[i] = '\0';

  return env_str;
}
