/** @file

  This file implements the rolled log deletion.

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

#include <climits>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "RolledLogDeleter.h"
#include "LogUtils.h"
#include "tscore/ts_file.h"
#include "tscpp/util/TextView.h"

namespace fs = ts::file;

LogDeletingInfo::LogDeletingInfo(const char *_logname, int _min_count)
  : logname(_logname),
    /**
     * A min_count of zero indicates a request to try to keep all rotated logs
     * around. By setting min_count to INT_MAX in these cases, we make the rolled
     * log deletion priority small.
     *
     * @note This cannot have a zero value because it is used as the denominator
     * in a division operation when calculating the log deletion preference.
     */
    min_count((_min_count > 0) ? _min_count : INT_MAX)
{
}

LogDeletingInfo::LogDeletingInfo(std::string_view _logname, int _min_count)
  : logname(_logname),
    /**
     * A min_count of zero indicates a request to try to keep all rotated logs
     * around. By setting min_count to INT_MAX in these cases, we make the rolled
     * log deletion priority small.
     *
     * @note This cannot have a zero value because it is used as the denominator
     * in a division operation when calculating the log deletion preference.
     */
    min_count((_min_count > 0) ? _min_count : INT_MAX)
{
}

void
RolledLogDeleter::register_log_type_for_deletion(std::string_view log_type, int rolling_min_count)
{
  if (deleting_info.find(log_type) != deleting_info.end()) {
    // Already registered.
    return;
  }
  auto deletingInfo     = std::make_unique<LogDeletingInfo>(log_type, rolling_min_count);
  auto *deletingInfoPtr = deletingInfo.get();

  deletingInfoList.push_back(std::move(deletingInfo));
  deleting_info.insert(deletingInfoPtr);
  candidates_require_sorting = true;
}

bool
RolledLogDeleter::consider_for_candidacy(std::string_view log_path, int64_t file_size, time_t modification_time)
{
  const fs::path rolled_log_file = fs::filename(log_path);
  auto iter                      = deleting_info.find(LogUtils::get_unrolled_filename(rolled_log_file.view()));
  if (iter == deleting_info.end()) {
    return false;
  }
  auto &candidates = iter->candidates;
  candidates.push_back(std::make_unique<LogDeleteCandidate>(log_path, file_size, modification_time));
  ++num_candidates;
  candidates_require_sorting = true;
  return true;
}

void
RolledLogDeleter::sort_candidates()
{
  deleting_info.apply([](LogDeletingInfo &info) {
    std::sort(info.candidates.begin(), info.candidates.end(),
              [](std::unique_ptr<LogDeleteCandidate> const &a, std::unique_ptr<LogDeleteCandidate> const &b) {
                return a->mtime > b->mtime;
              });
  });
  candidates_require_sorting = false;
}

std::unique_ptr<LogDeleteCandidate>
RolledLogDeleter::take_next_candidate_to_delete()
{
  if (!has_candidates()) {
    return nullptr;
  }
  if (candidates_require_sorting) {
    sort_candidates();
  }
  // Select the highest priority type (diags.log, traffic.out, etc.) from which
  // to select a candidate.
  auto target_type =
    std::max_element(deleting_info.begin(), deleting_info.end(), [](LogDeletingInfo const &A, LogDeletingInfo const &B) {
      return static_cast<double>(A.candidates.size()) / A.min_count < static_cast<double>(B.candidates.size()) / B.min_count;
    });

  auto &candidates = target_type->candidates;
  if (candidates.empty()) {
    return nullptr;
  }

  // Return the highest priority candidate among the candidates of that type.
  auto victim = std::move(candidates.back());
  candidates.pop_back();
  --num_candidates;
  return victim;
}

bool
RolledLogDeleter::has_candidates() const
{
  return get_candidate_count() != 0;
}

size_t
RolledLogDeleter::get_candidate_count() const
{
  return num_candidates;
}

void
RolledLogDeleter::clear_candidates()
{
  deleting_info.apply([](LogDeletingInfo &info) { info.clear(); });
  num_candidates = 0;
}
