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

#include "UmsHelper.h"
#include "StringHash.h"

ums_helper::ums_helper():empty_list(NULL), unique_list(NULL), hash_table(NULL), min_path_size(0), max_path_size(0), map_cnt(0),
tag_present(false)
{
}

void
ums_helper::delete_hash_table(void)
{
  if (hash_table) {
    delete hash_table;
    hash_table = NULL;
  }
}

ums_helper::~ums_helper()
{
  delete_hash_table();
}

StringHash *
ums_helper::init_hash_table(int _map_cnt)
{
  int hash_tbl_size;
  delete_hash_table();
  if (_map_cnt < 0)
    _map_cnt = map_cnt;
  if (unlikely((hash_tbl_size = (_map_cnt << 5)) > STRINGHASH_MAX_TBL_SIZE)) {
    hash_tbl_size = STRINGHASH_MAX_TBL_SIZE;
  }
  hash_table = NEW(new StringHash(hash_tbl_size));
  return hash_table;
}

int
ums_helper::load_hash_table(url_mapping * list)
{
  url_mapping *ul, *ut;
  StringHashEntry *he;
  const char *from_path;
  int from_path_len, load_cnt = 0;

  if (likely(hash_table)) {
    for (ul = list; ul; ul = ul->next_schema) {
      ul->next_hash = NULL;
      if ((from_path = ul->fromURL.path_get(&from_path_len)) != NULL) {
        if (from_path_len > min_path_size)
          from_path_len = min_path_size;
        if (likely((he = hash_table->find_or_add(ul, from_path, from_path_len)) != NULL) && he->ptr != (void *) ul) {
          for (ut = (url_mapping *) he->ptr; ut->next_hash;)
            ut = ut->next_hash;
          ut->next_hash = ul;
        }
      }
      load_cnt++;
    }
  }
  return load_cnt;
}

url_mapping *
ums_helper::lookup_best_empty(const char *request_host, int request_port, char *tag)
{
  url_mapping *ht_entry = empty_list;

  if (unlikely(tag_present)) {
    for (; ht_entry; ht_entry = ht_entry->next_schema) {
      bool tags_match = (ht_entry->tag && (!tag || strcmp(tag, ht_entry->tag))) ? false : true;
      if (tags_match && (*request_host == '\0' || request_port == ht_entry->fromURL.port_get()))
        break;
    }
  } else {
    for (; ht_entry; ht_entry = ht_entry->next_schema) {
      if (*request_host == '\0' || request_port == ht_entry->fromURL.port_get())
        break;
    }
  }
  return ht_entry;
}

url_mapping *
ums_helper::lookup_best_notempty(url_mapping * ht_entry, const char *request_host, int request_port,
                                 const char *request_path, int request_path_len, char *tag)
{
  bool tags_match;
  const char *from_path;
  int from_path_len, tmp;
  StringHashEntry *he;

  if (unique_list)              // most complicated but very rare case - unique_list != 0
  {
    for (; ht_entry; ht_entry = ht_entry->next_schema) {
      tags_match = (ht_entry->tag && (!tag || strcmp(tag, ht_entry->tag))) ? false : true;
      if (tags_match && (!request_host[0] || request_port == ht_entry->fromURL.port_get())) {
        from_path = ht_entry->fromURL.path_get(&from_path_len);
        if (ht_entry->unique) {
          if (from_path && from_path_len == request_path_len && !memcmp(from_path, request_path, from_path_len))
            break;
        } else if (!from_path || (request_path_len >= from_path_len && !memcmp(from_path, request_path, from_path_len)))
          break;
      }
    }
    return ht_entry;
  }
  // unique_list is empty
  if (empty_list)               // unique_list == 0 && empty_list != 0
  {
    for (; ht_entry; ht_entry = ht_entry->next_schema) {
      tags_match = (ht_entry->tag && (!tag || strcmp(tag, ht_entry->tag))) ? false : true;
      if (tags_match && (!request_host[0] || request_port == ht_entry->fromURL.port_get())) {
        from_path = ht_entry->fromURL.path_get(&from_path_len);
        if (!from_path || (request_path_len >= from_path_len && !memcmp(from_path, request_path, from_path_len)))
          break;
      }
    }
    return ht_entry;
  }
  // lh->unique_list == 0 && lh->empty_list == 0
  if (likely((tmp = request_path_len) >= min_path_size)) {
    if (hash_table)             // the best possible case from a performance point of view - we can use sorted hash
    {
      int lookup_size = (tmp > min_path_size) ? min_path_size : tmp;
      if ((he = hash_table->find_or_add(0, request_path, lookup_size)) != NULL) {
        if (tag_present) {
          for (ht_entry = (url_mapping *) he->ptr; ht_entry; ht_entry = ht_entry->next_hash) {
            tags_match = (ht_entry->tag && (!tag || strcmp(tag, ht_entry->tag))) ? false : true;
            if (tags_match && (!request_host[0] || request_port == ht_entry->fromURL.port_get())) {
              from_path = ht_entry->fromURL.path_get(&from_path_len);
              if (request_path_len >= from_path_len && !memcmp(from_path, request_path, from_path_len))
                break;
            }
          }
        } else {
          for (ht_entry = (url_mapping *) he->ptr; ht_entry; ht_entry = ht_entry->next_hash) {
            if (!request_host[0] || request_port == ht_entry->fromURL.port_get()) {
              from_path = ht_entry->fromURL.path_get(&from_path_len);
              if (request_path_len >= from_path_len && !memcmp(from_path, request_path, from_path_len))
                break;
            }
          }
        }
      } else
        ht_entry = NULL;
    } else                      // there is no lookup table - very limited search (from one item)
    {
      if (tag_present) {
        for (; ht_entry; ht_entry = ht_entry->next_schema) {
          tags_match = (ht_entry->tag && (!tag || strcmp(tag, ht_entry->tag))) ? false : true;
          if (tags_match && (!request_host[0] || request_port == ht_entry->fromURL.port_get())) {
            from_path = ht_entry->fromURL.path_get(&from_path_len);
            if (request_path_len >= from_path_len && !memcmp(from_path, request_path, from_path_len))
              break;
          }
        }
      } else {
        for (; ht_entry; ht_entry = ht_entry->next_schema) {
          if ((!request_host[0] || request_port == ht_entry->fromURL.port_get())) {
            from_path = ht_entry->fromURL.path_get(&from_path_len);
            if (request_path_len >= from_path_len && !memcmp(from_path, request_path, from_path_len))
              break;
          }
        }
      }
    }
    return ht_entry;
  }
  return NULL;
}
