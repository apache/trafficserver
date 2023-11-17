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

  Store.h


 ****************************************************************************/

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/Result.h"

#define STORE_BLOCK_SIZE       8192
#define STORE_BLOCK_SHIFT      13
#define DEFAULT_HW_SECTOR_SIZE 512

enum span_error_t {
  SPAN_ERROR_OK,
  SPAN_ERROR_UNKNOWN,
  SPAN_ERROR_NOT_FOUND,
  SPAN_ERROR_NO_ACCESS,
  SPAN_ERROR_MISSING_SIZE,
  SPAN_ERROR_UNSUPPORTED_DEVTYPE,
  SPAN_ERROR_MEDIA_PROBE,
};

struct span_diskid_t {
  int64_t id[2];

  bool
  operator<(const span_diskid_t &rhs) const
  {
    return id[0] < rhs.id[0] && id[1] < rhs.id[1];
  }

  bool
  operator==(const span_diskid_t &rhs) const
  {
    return id[0] == rhs.id[0] && id[1] == rhs.id[1];
  }

  int64_t &
  operator[](unsigned i)
  {
    return id[i];
  }
};

//
// A Store is a place to store data.
// Those on the same disk should be in a linked list.
//
struct Span {
  int64_t blocks          = 0; // in STORE_BLOCK_SIZE blocks
  int64_t offset          = 0; // used only if (file == true); in bytes
  unsigned hw_sector_size = DEFAULT_HW_SECTOR_SIZE;
  unsigned alignment      = 0;
  span_diskid_t disk_id;
  bool file_pathname = false; // the pathname is a file
  // v- used as a magic location for copy constructor.
  // we memcpy everything before this member and do explicit assignment for the rest.
  ats_scoped_str id;
  ats_scoped_str pathname;
  ats_scoped_str hash_base_string; ///< Used to seed the stripe assignment hash.

  SLINK(Span, link);

  int64_t
  size() const
  {
    return blocks * STORE_BLOCK_SIZE;
  }

  int64_t
  end() const
  {
    return offset + blocks;
  }

  const char *init(const char *id, const char *path, int64_t size);

  /// Set the hash seed string.
  void hash_base_string_set(const char *s);
  /// Set the volume number.
  void volume_number_set(int n);

  Span() { disk_id[0] = disk_id[1] = 0; }

  /// Copy constructor.
  /// @internal Prior to this implementation handling the char* pointers was done manually
  /// at every call site. We also need this because we have @c ats_scoped_str members and need
  /// to make copies.
  Span(Span const &that)
  {
    /* I looked at simplifying this by changing the @c ats_scoped_str instances to @c std::string
     * but that actually makes it worse. The copy constructor @b must be overridden to get the
     * internal link (@a link.next) correct. Given that, changing to @c std::string means doing
     * explicit assignment for every member, which has its own problems.
     */
    memcpy(static_cast<void *>(this), &that, reinterpret_cast<intptr_t>(&(static_cast<Span *>(nullptr)->pathname)));
    if (that.id) {
      id = ats_strdup(that.id);
    }
    if (that.pathname) {
      pathname = ats_strdup(that.pathname);
    }
    if (that.hash_base_string) {
      hash_base_string = ats_strdup(that.hash_base_string);
    }
    link.next = nullptr;
  }

  ~Span();

  static const char *errorstr(span_error_t serr);
};

struct Store {
  void sort();

  void
  extend(unsigned i)
  {
    if (i > n_spans) {
      spans = static_cast<Span **>(ats_realloc(spans, i * sizeof(Span *)));
      for (unsigned j = n_spans; j < i; j++) {
        spans[j] = nullptr;
      }
      n_spans = i;
    }
  }

  void delete_all();

  Store(){};
  ~Store();

  unsigned n_spans_in_config = 0; ///< The number of disks/paths defined in storage.yaml
  unsigned n_spans           = 0; ///< The number of disks/paths we could actually read and parse
  Span **spans               = nullptr;

  Result read_config();

  int write_config_data(int fd) const;
};

// store either free or in the cache, can be stolen for reconfiguration
void stealStore(Store &s, int blocks);
