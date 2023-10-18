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

#pragma once

// cache stats definitions, for both global cache metrics, as well as per volume metrics.
enum class CacheOpType { Lookup = 0, Read, Write, Update, Remove, Evacuate, Scan, Last };

struct CacheStatsBlock {
  struct {
    ts::Metrics::IntType *active  = nullptr;
    ts::Metrics::IntType *success = nullptr;
    ts::Metrics::IntType *failure = nullptr;
  } status[static_cast<int>(CacheOpType::Last)];

  ts::Metrics::IntType *fragment_document_count[3] = {nullptr, nullptr, nullptr}; // For 1, 2 and 3+ fragments

  ts::Metrics::IntType *bytes_used                = nullptr;
  ts::Metrics::IntType *bytes_total               = nullptr;
  ts::Metrics::IntType *stripes                   = nullptr;
  ts::Metrics::IntType *ram_cache_bytes           = nullptr;
  ts::Metrics::IntType *ram_cache_bytes_total     = nullptr;
  ts::Metrics::IntType *direntries_total          = nullptr;
  ts::Metrics::IntType *direntries_used           = nullptr;
  ts::Metrics::IntType *ram_cache_hits            = nullptr;
  ts::Metrics::IntType *ram_cache_misses          = nullptr;
  ts::Metrics::IntType *pread_count               = nullptr;
  ts::Metrics::IntType *percent_full              = nullptr;
  ts::Metrics::IntType *read_seek_fail            = nullptr;
  ts::Metrics::IntType *read_invalid              = nullptr;
  ts::Metrics::IntType *write_backlog_failure     = nullptr;
  ts::Metrics::IntType *directory_collision_count = nullptr;
  ts::Metrics::IntType *read_busy_success         = nullptr;
  ts::Metrics::IntType *read_busy_failure         = nullptr;
  ts::Metrics::IntType *gc_bytes_evacuated        = nullptr;
  ts::Metrics::IntType *gc_frags_evacuated        = nullptr;
  ts::Metrics::IntType *write_bytes               = nullptr;
  ts::Metrics::IntType *hdr_vector_marshal        = nullptr;
  ts::Metrics::IntType *hdr_marshal               = nullptr;
  ts::Metrics::IntType *hdr_marshal_bytes         = nullptr;
  ts::Metrics::IntType *directory_wrap            = nullptr;
  ts::Metrics::IntType *directory_sync_count      = nullptr;
  ts::Metrics::IntType *directory_sync_time       = nullptr;
  ts::Metrics::IntType *directory_sync_bytes      = nullptr;
  ts::Metrics::IntType *span_errors_read          = nullptr;
  ts::Metrics::IntType *span_errors_write         = nullptr;
  ts::Metrics::IntType *span_offline              = nullptr;
  ts::Metrics::IntType *span_online               = nullptr;
  ts::Metrics::IntType *span_failing              = nullptr;
};
