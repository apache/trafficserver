/** @file

  This contains the rotated log deletion mechanism.

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

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <time.h>
#include <vector>

#include "tscore/IntrusiveHashMap.h"

/*-------------------------------------------------------------------------
  LogDeleteCandidate, LogDeletingInfo&Descriptor
  -------------------------------------------------------------------------*/

struct LogDeleteCandidate {
  /** The filename for this rolled log deletion candidate.
   *
   * For example: /var/log/my_log.log_a_host_name.20191122.20h18m35s-20191122.20h18m51s.old
   */
  std::string rolled_log_path;
  int64_t size;
  time_t mtime;

  LogDeleteCandidate(std::string_view p_name, int64_t st_size, time_t st_time)
    : rolled_log_path(p_name), size(st_size), mtime(st_time)
  {
  }
};

/** Configure rolled log deletion for a set of logs.
 *
 * This contains the configuration and set of log deletion candidates for a set
 * of log files associated with logname. There will be an instance of this for
 * diags.log and its associated rolled log files, one for traffic.out, etc.
 */
struct LogDeletingInfo {
  /** The unrolled log name (such as "diags.log"). */
  const std::string logname;

  /** The minimum number of rolled log files to try to keep around.
   *
   * @note This is guaranteed to be a positive (non-zero) value.
   */
  int min_count;

  std::vector<std::unique_ptr<LogDeleteCandidate>> candidates;

  LogDeletingInfo *_next{nullptr};
  LogDeletingInfo *_prev{nullptr};

  /**
   * @param[in] logname The unrolled log name.
   *
   * @param[in] min_count The minimum number of rolled files to try to keep
   * around when deleting rolled logs. A zero indicates a desire to keep all
   * rolled logs around.
   *
   * @note The min_count is used as a part of a calculation to determine which
   * set of deletion candidates should be used for selecting a rolled log file
   * to delete. If space is particularly constrained, even LogDeletingInfo
   * instances with a min_count of 0 may be selected for deletion.
   */
  LogDeletingInfo(const char *logname, int min_count);
  LogDeletingInfo(std::string_view logname, int min_count);

  void
  clear()
  {
    candidates.clear();
  }
};

struct LogDeletingInfoDescriptor {
  using key_type   = std::string_view;
  using value_type = LogDeletingInfo;

  static key_type
  key_of(value_type *value)
  {
    return value->logname;
  }

  static bool
  equal(key_type const &lhs, key_type const &rhs)
  {
    return lhs == rhs;
  }

  static value_type *&
  next_ptr(value_type *value)
  {
    return value->_next;
  }

  static value_type *&
  prev_ptr(value_type *value)
  {
    return value->_prev;
  }

  static constexpr std::hash<std::string_view> hasher{};

  static auto
  hash_of(key_type s) -> decltype(hasher(s))
  {
    return hasher(s);
  }
};

/**
 * RolledLogDeleter is responsible for keeping track of rolled log candidates
 * and presenting them for deletion in a prioritized order based on size and
 * last modified time stamp.
 *
 * Terminology:
 *
 * log type: An unrolled log name that represents a category of rolled log
 * files that are candidates for deletion. This may be something like
 * diags.log, traffic.out, etc.
 *
 * candidate: A rolled log file which is a candidate for deletion at some
 * point. This may be something like:
 *   squid.log_some.hostname.com.20191125.19h00m04s-20191125.19h15m04s.old.
 */
class RolledLogDeleter
{
public:
  /** Register a new log type for candidates for log deletion.
   *
   * @param[in] log_type The unrolled name for a set of rolled log files to
   * consider for deletion. This may be something like diags.log, for example.
   *
   * @param[in] rolling_min_count The minimum number of rolled log files to
   * keep around.
   */
  void register_log_type_for_deletion(std::string_view log_type, int rolling_min_count);

  /** Evaluate a rolled log file to see whether it is a candidate for deletion.
   *
   * If the rolled log file is a valid candidate, it will be stored and considered
   * for deletion upon later calls to deleteALogFile.
   *
   * @param[in] log_path The rolled log file path.
   *
   * @param[in] file_size The size of the rolled log file.
   *
   * @param[in] modification_time The time the rolled log file was last modified.
   * candidate for deletion.
   *
   * @return True if the rolled log file is a deletion candidate, false otherwise.
   */
  bool consider_for_candidacy(std::string_view log_path, int64_t file_size, time_t modification_time);

  /** Retrieve the next rolled log file to delete.
   *
   * This removes the returned rolled file from the candidates list.
   *
   * @return The next rolled log candidate to delete or nullptr if there is no
   * such candidate.
   */
  std::unique_ptr<LogDeleteCandidate> take_next_candidate_to_delete();

  /** Whether there are any candidates for possible deletion.
   *
   * @return True if there are candidates for deletion, false otherwise.
   */
  bool has_candidates() const;

  /** Retrieve the number of rolled log deletion candidates.
   *
   * @return The number of rolled logs that are candidates for deletion.
   */
  size_t get_candidate_count() const;

  /** Clear the internal candidates array.
   */
  void clear_candidates();

private:
  /** Sort all the assembled candidates for each LogDeletingInfo.
   *
   * After any additions to the @a deleting_info, this should be called before
   * calling @a take_next_candidate_to_delete because the latter depends upon
   * the candidate entries being sorted.
   */
  void sort_candidates();

private:
  /** The owning references to the set of LogDeletingInfo added to the below
   * hash map. */
  std::deque<std::unique_ptr<LogDeletingInfo>> deletingInfoList;

  /** The set of candidates for deletion keyed by log_type. */
  IntrusiveHashMap<LogDeletingInfoDescriptor> deleting_info;

  /** The number of tracked candidates. */
  size_t num_candidates = 0;

  /** Whether the candidates require sorting due to an addition to the
   * deleting_info. */
  bool candidates_require_sorting = true;
};
