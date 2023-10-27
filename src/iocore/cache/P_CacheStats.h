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
    Metrics::Counter::AtomicType *active  = nullptr;
    Metrics::Counter::AtomicType *success = nullptr;
    Metrics::Counter::AtomicType *failure = nullptr;
  } status[static_cast<int>(CacheOpType::Last)];

  Metrics::Counter::AtomicType *fragment_document_count[3] = {nullptr, nullptr, nullptr}; // For 1, 2 and 3+ fragments

  Metrics::Gauge::AtomicType *bytes_used              = nullptr;
  Metrics::Gauge::AtomicType *bytes_total             = nullptr;
  Metrics::Gauge::AtomicType *stripes                 = nullptr;
  Metrics::Counter::AtomicType *ram_cache_bytes       = nullptr;
  Metrics::Gauge::AtomicType *ram_cache_bytes_total   = nullptr;
  Metrics::Gauge::AtomicType *direntries_total        = nullptr;
  Metrics::Gauge::AtomicType *direntries_used         = nullptr;
  Metrics::Counter::AtomicType *ram_cache_hits        = nullptr;
  Metrics::Counter::AtomicType *ram_cache_misses      = nullptr;
  Metrics::Counter::AtomicType *pread_count           = nullptr;
  Metrics::Gauge::AtomicType *percent_full            = nullptr;
  Metrics::Counter::AtomicType *read_seek_fail        = nullptr;
  Metrics::Counter::AtomicType *read_invalid          = nullptr;
  Metrics::Counter::AtomicType *write_backlog_failure = nullptr;
  Metrics::Counter::AtomicType *directory_collision   = nullptr;
  Metrics::Counter::AtomicType *read_busy_success     = nullptr;
  Metrics::Counter::AtomicType *read_busy_failure     = nullptr;
  Metrics::Counter::AtomicType *gc_bytes_evacuated    = nullptr;
  Metrics::Counter::AtomicType *gc_frags_evacuated    = nullptr;
  Metrics::Counter::AtomicType *write_bytes           = nullptr;
  Metrics::Counter::AtomicType *hdr_vector_marshal    = nullptr;
  Metrics::Counter::AtomicType *hdr_marshal           = nullptr;
  Metrics::Counter::AtomicType *hdr_marshal_bytes     = nullptr;
  Metrics::Counter::AtomicType *directory_wrap        = nullptr;
  Metrics::Counter::AtomicType *directory_sync_count  = nullptr;
  Metrics::Counter::AtomicType *directory_sync_time   = nullptr;
  Metrics::Counter::AtomicType *directory_sync_bytes  = nullptr;
  Metrics::Counter::AtomicType *span_errors_read      = nullptr;
  Metrics::Counter::AtomicType *span_errors_write     = nullptr;
  Metrics::Gauge::AtomicType *span_offline            = nullptr;
  Metrics::Gauge::AtomicType *span_online             = nullptr;
  Metrics::Gauge::AtomicType *span_failing            = nullptr;
};
