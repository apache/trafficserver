/** @file
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

#pragma once

#include "slice.h"
#include "ObjectSizeCache.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include <string>
#include <mutex>

// Data Structures and Classes
struct Config {
  static constexpr int64_t const blockbytesmin     = 1024 * 256;        // 256KB
  static constexpr int64_t const blockbytesmax     = 1024 * 1024 * 128; // 128MB
  static constexpr int64_t const blockbytesdefault = 1024 * 1024;       // 1MB

  int64_t     m_blockbytes{blockbytesdefault};
  std::string m_remaphost; // remap host to use for loopback slice GET
  std::string m_regexstr;  // regex string for things to slice (default all)
  enum RegexType { None, Include, Exclude };
  RegexType   m_regex_type{None};
  pcre       *m_regex{nullptr};
  pcre_extra *m_regex_extra{nullptr};
  int         m_paceerrsecs{0};   // -1 disable logging, 0 no pacing, max 60s
  int         m_prefetchcount{0}; // 0 disables prefetching
  enum RefType { First, Relative };
  RefType  m_reftype{First};          // reference slice is relative to request
  bool     m_head_strip_range{false}; // strip range header for head requests
  uint64_t m_min_size_to_slice{0};    // Only strip objects larger than this

  std::string m_skip_header;
  std::string m_crr_ims_header;

  // Convert optarg to bytes
  static int64_t bytesFrom(char const *const valstr);

  // clean up pcre if applicable
  ~Config();

  // Parse from args, ast one wins
  bool fromArgs(int const argc, char const *const argv[]);

  // Check if the error should can be logged, if successful may update m_nexttime
  bool canLogError();

  // Check if regex supplied
  bool
  hasRegex() const
  {
    return None != m_regex_type;
  }

  // If no null reg, true, otherwise check against regex
  bool matchesRegex(char const *const url, int const urllen) const;

  // Add an object size to cache
  void sizeCacheAdd(std::string_view url, uint64_t size);

  // Remove an object size
  void sizeCacheRemove(std::string_view url);

  // Did we cache this internally as a small object?
  bool isKnownLargeObj(std::string_view url);

  // Metadata cache stats
  std::string stat_prefix{};
  int         stat_TP{0}, stat_TN{0}, stat_FP{0}, stat_FN{0}, stat_no_cl{0}, stat_bad_cl{0}, stat_no_url{0};
  bool        stats_enabled{false};

private:
  TSHRTime                       m_nextlogtime{0}; // next time to log in ns
  std::mutex                     m_mutex;
  std::optional<ObjectSizeCache> m_oscache;
  void                           setCacheSize(size_t entries);
};
