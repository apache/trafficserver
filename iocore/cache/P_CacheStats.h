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
    ts::Metrics::IntType *active;
    ts::Metrics::IntType *success;
    ts::Metrics::IntType *failure;
  } status[static_cast<int>(CacheOpType::Last)];

  ts::Metrics::IntType *fragment_document_count[3]; // For 1, 2 and 3+ fragments

  ts::Metrics::IntType *bytes_used;
  ts::Metrics::IntType *bytes_total;
  ts::Metrics::IntType *stripes;
  ts::Metrics::IntType *ram_cache_bytes;
  ts::Metrics::IntType *ram_cache_bytes_total;
  ts::Metrics::IntType *direntries_total;
  ts::Metrics::IntType *direntries_used;
  ts::Metrics::IntType *ram_cache_hits;
  ts::Metrics::IntType *ram_cache_misses;
  ts::Metrics::IntType *pread_count;
  ts::Metrics::IntType *percent_full;
  ts::Metrics::IntType *read_seek_fail;
  ts::Metrics::IntType *read_invalid;
  ts::Metrics::IntType *write_backlog_failure;
  ts::Metrics::IntType *directory_collision_count;
  ts::Metrics::IntType *read_busy_success;
  ts::Metrics::IntType *read_busy_failure;
  ts::Metrics::IntType *gc_bytes_evacuated;
  ts::Metrics::IntType *gc_frags_evacuated;
  ts::Metrics::IntType *write_bytes;
  ts::Metrics::IntType *hdr_vector_marshal;
  ts::Metrics::IntType *hdr_marshal;
  ts::Metrics::IntType *hdr_marshal_bytes;
  ts::Metrics::IntType *directory_wrap;
  ts::Metrics::IntType *directory_sync_count;
  ts::Metrics::IntType *directory_sync_time;
  ts::Metrics::IntType *directory_sync_bytes;
  ts::Metrics::IntType *span_errors_read;
  ts::Metrics::IntType *span_errors_write;
  ts::Metrics::IntType *span_offline;
  ts::Metrics::IntType *span_online;
  ts::Metrics::IntType *span_failing;
};
