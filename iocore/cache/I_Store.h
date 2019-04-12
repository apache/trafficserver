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

#define STORE_BLOCK_SIZE 8192
#define STORE_BLOCK_SHIFT 13
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

  int64_t &operator[](unsigned i) { return id[i]; }
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
  int forced_volume_num = -1; ///< Force span in to specific volume.
private:
  bool is_mmapable_internal = false;

public:
  bool file_pathname = false; // the pathname is a file
  // v- used as a magic location for copy constructor.
  // we memcpy everything before this member and do explicit assignment for the rest.
  ats_scoped_str pathname;
  ats_scoped_str hash_base_string; ///< Used to seed the stripe assignment hash.
  SLINK(Span, link);

  bool
  is_mmapable() const
  {
    return is_mmapable_internal;
  }
  void
  set_mmapable(bool s)
  {
    is_mmapable_internal = s;
  }
  int64_t
  size() const
  {
    return blocks * STORE_BLOCK_SIZE;
  }

  int64_t
  total_blocks() const
  {
    if (link.next) {
      return blocks + link.next->total_blocks();
    } else {
      return blocks;
    }
  }

  Span *
  nth(unsigned i)
  {
    Span *x = this;
    while (x && i--) {
      x = x->link.next;
    }
    return x;
  }

  unsigned
  paths() const
  {
    int i = 0;
    for (const Span *x = this; x; i++, x = x->link.next) {
      ;
    }

    return i;
  }

  int write(int fd) const;
  int read(int fd);

  /// Duplicate this span and all chained spans.
  Span *dup();
  int64_t
  end() const
  {
    return offset + blocks;
  }

  const char *init(const char *n, int64_t size);

  // 0 on success -1 on failure
  int path(char *filename,         // for non-file, the filename in the director
           int64_t *offset,        // for file, start offset (unsupported)
           char *buf, int buflen); // where to store the path

  /// Set the hash seed string.
  void hash_base_string_set(const char *s);
  /// Set the volume number.
  void volume_number_set(int n);

  Span() { disk_id[0] = disk_id[1] = 0; }

  /// Copy constructor.
  /// @internal Prior to this implementation handling the char* pointers was done manual
  /// at every call site. We also need this because we have ats_scoped_str members.
  Span(Span const &that)
  {
    memcpy(this, &that, reinterpret_cast<intptr_t>(&(static_cast<Span *>(nullptr)->pathname)));
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
  //
  // Public Interface
  // Thread-safe operations
  //

  // spread evenly on all disks
  void spread_alloc(Store &s, unsigned int blocks, bool mmapable = true);
  void alloc(Store &s, unsigned int blocks, bool only_one = false, bool mmapable = true);

  Span *
  alloc_one(unsigned int blocks, bool mmapable)
  {
    Store s;
    alloc(s, blocks, true, mmapable);
    if (s.n_disks) {
      Span *t   = s.disk[0];
      s.disk[0] = nullptr;
      return t;
    }

    return nullptr;
  }
  // try to allocate, return (s == gotten, diff == not gotten)
  void try_realloc(Store &s, Store &diff);

  // free back the contents of a store.
  // must have been JUST allocated (no intervening allocs/frees)
  void free(Store &s);
  void add(Span *s);
  void add(Store &s);
  void dup(Store &s);
  void sort();
  void
  extend(unsigned i)
  {
    if (i > n_disks) {
      disk = (Span **)ats_realloc(disk, i * sizeof(Span *));
      for (unsigned j = n_disks; j < i; j++) {
        disk[j] = nullptr;
      }
      n_disks = i;
    }
  }

  // Non Thread-safe operations
  unsigned int
  total_blocks(unsigned after = 0) const
  {
    int64_t t = 0;
    for (unsigned i = after; i < n_disks; i++) {
      if (disk[i]) {
        t += disk[i]->total_blocks();
      }
    }
    return (unsigned int)t;
  }
  // 0 on success -1 on failure
  // these operations are NOT thread-safe
  //
  int write(int fd, const char *name) const;
  int read(int fd, char *name);
  int clear(char *filename, bool clear_dirs = true);
  void normalize();
  void delete_all();
  int remove(char *pathname);
  Store();
  ~Store();

  // The number of disks/paths defined in storage.config
  unsigned n_disks_in_config = 0;
  // The number of disks/paths we could actually read and parse.
  unsigned n_disks = 0;
  Span **disk      = nullptr;

  Result read_config();

  int write_config_data(int fd) const;

  /// Additional configuration key values.
  static const char VOLUME_KEY[];
  static const char HASH_BASE_STRING_KEY[];
};

// store either free or in the cache, can be stolen for reconfiguration
void stealStore(Store &s, int blocks);
