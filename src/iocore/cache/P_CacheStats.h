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
    Counter::AtomicType *active  = nullptr;
    Counter::AtomicType *success = nullptr;
    Counter::AtomicType *failure = nullptr;
  } status[static_cast<int>(CacheOpType::Last)];

  Counter::AtomicType *fragment_document_count[3] = {nullptr, nullptr, nullptr}; // For 1, 2 and 3+ fragments

  Counter::AtomicType *bytes_used                = nullptr;
  Counter::AtomicType *bytes_total               = nullptr;
  Counter::AtomicType *stripes                   = nullptr;
  Counter::AtomicType *ram_cache_bytes           = nullptr;
  Counter::AtomicType *ram_cache_bytes_total     = nullptr;
  Counter::AtomicType *direntries_total          = nullptr;
  Counter::AtomicType *direntries_used           = nullptr;
  Counter::AtomicType *ram_cache_hits            = nullptr;
  Counter::AtomicType *ram_cache_misses          = nullptr;
  Counter::AtomicType *pread_count               = nullptr;
  Counter::AtomicType *percent_full              = nullptr;
  Counter::AtomicType *read_seek_fail            = nullptr;
  Counter::AtomicType *read_invalid              = nullptr;
  Counter::AtomicType *write_backlog_failure     = nullptr;
  Counter::AtomicType *directory_collision_count = nullptr;
  Counter::AtomicType *read_busy_success         = nullptr;
  Counter::AtomicType *read_busy_failure         = nullptr;
  Counter::AtomicType *gc_bytes_evacuated        = nullptr;
  Counter::AtomicType *gc_frags_evacuated        = nullptr;
  Counter::AtomicType *write_bytes               = nullptr;
  Counter::AtomicType *hdr_vector_marshal        = nullptr;
  Counter::AtomicType *hdr_marshal               = nullptr;
  Counter::AtomicType *hdr_marshal_bytes         = nullptr;
  Counter::AtomicType *directory_wrap            = nullptr;
  Counter::AtomicType *directory_sync_count      = nullptr;
  Counter::AtomicType *directory_sync_time       = nullptr;
  Counter::AtomicType *directory_sync_bytes      = nullptr;
  Counter::AtomicType *span_errors_read          = nullptr;
  Counter::AtomicType *span_errors_write         = nullptr;
  Counter::AtomicType *span_offline              = nullptr;
  Counter::AtomicType *span_online               = nullptr;
  Counter::AtomicType *span_failing              = nullptr;
};
