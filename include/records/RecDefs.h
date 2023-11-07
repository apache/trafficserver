/** @file

  Public Rec defines and types

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

#include "tscore/ink_mutex.h"
#include "tscore/ink_rwlock.h"
#include "records/RecMutex.h"

#include <functional>

//-------------------------------------------------------------------------
// Error Values
//-------------------------------------------------------------------------
enum RecErrT {
  REC_ERR_FAIL = -1,
  REC_ERR_OKAY = 0,
};

//-------------------------------------------------------------------------
// Types
//-------------------------------------------------------------------------
#define RecStringNull nullptr

using RecInt         = int64_t;
using RecFloat       = float;
using RecString      = char *;
using RecStringConst = const char *;
using RecCounter     = int64_t;
using RecByte        = int8_t;
using RecBool        = bool;

enum RecT {
  RECT_NULL    = 0x00,
  RECT_CONFIG  = 0x01,
  RECT_PROCESS = 0x02,
  RECT_NODE    = 0x04,
  RECT_LOCAL   = 0x10,
  RECT_PLUGIN  = 0x20,
  RECT_ALL     = 0x3F
};

enum RecDataT {
  RECD_NULL = 0,
  RECD_INT,
  RECD_FLOAT,
  RECD_STRING,
  RECD_COUNTER,

  RECD_MAX
};

enum RecPersistT {
  RECP_NULL,
  RECP_PERSISTENT,
  RECP_NON_PERSISTENT,
};

// RECP_NULL should never be used by callers of RecRegisterStat*(). You have to decide
// whether to persist stats or not. The template goop below make sure that passing RECP_NULL
// is a very ugly compile-time error.

namespace rec
{
namespace detail
{
  template <RecPersistT> struct is_valid_persistence;

  template <> struct is_valid_persistence<RECP_PERSISTENT> {
    static const RecPersistT value = RECP_PERSISTENT;
  };

  template <> struct is_valid_persistence<RECP_NON_PERSISTENT> {
    static const RecPersistT value = RECP_NON_PERSISTENT;
  };
} // namespace detail
} // namespace rec

#define REC_PERSISTENCE_TYPE(P) rec::detail::is_valid_persistence<P>::value

enum RecUpdateT {
  RECU_NULL,       // default: don't know the behavior
  RECU_DYNAMIC,    // config can be updated dynamically w/ "traffic_ctl config reload"
  RECU_RESTART_TS, // config requires TS to be restarted to take effect
  RECU_RESTART_TM, // deprecated
};

enum RecCheckT {
  RECC_NULL, // default: no check type defined
  RECC_STR,  // config is a string
  RECC_INT,  // config is an integer with a range
  RECC_IP    // config is an ip address
};

/// The source of the value.
/// @internal @c REC_SOURCE_NULL is useful for a return value, I don't see using it in the actual data.
/// @internal If this is changed, TSMgmtSource in apidefs.h.in must also be changed.
enum RecSourceT {
  REC_SOURCE_NULL,     ///< No source / value not set.
  REC_SOURCE_DEFAULT,  ///< Built in default.
  REC_SOURCE_PLUGIN,   ///< Plugin supplied default.
  REC_SOURCE_EXPLICIT, ///< Set by administrator (config file, external API, etc.)
  REC_SOURCE_ENV       ///< Process environment variable.
};

enum RecAccessT {
  RECA_NULL,
  RECA_NO_ACCESS,
  RECA_READ_ONLY,
};

//-------------------------------------------------------------------------
// Data Union
//-------------------------------------------------------------------------
union RecData {
  RecInt rec_int;
  RecFloat rec_float;
  RecString rec_string;
  RecCounter rec_counter;
};

//-------------------------------------------------------------------------
// RawStat Structures
//-------------------------------------------------------------------------
struct RecRawStat {
  int64_t sum;
  int64_t count;
  // XXX - these will waste some space because they are only needed for the globals
  // this is a fix for bug TS-162, so I am trying to do as few code changes as
  // possible, this should be revisited -bcall
  int64_t last_sum;   // value from the last global sync
  int64_t last_count; // value from the last global sync
  uint32_t version;
};

struct RecRawStatBlock;

// This defines the interface to the low level stat block operations
// The implementation of this was moved out of the records library due to a circular dependency this produced.
// look for the implementation of RecRawStatBlockOps in iocore/eventsystem
struct RecRawStatBlockOps {
  virtual ~RecRawStatBlockOps()                                                   = default;
  virtual int raw_stat_clear_sum(RecRawStatBlock *rsb, int id)                    = 0;
  virtual int raw_stat_clear_count(RecRawStatBlock *rsb, int id)                  = 0;
  virtual int raw_stat_get_total(RecRawStatBlock *rsb, int id, RecRawStat *total) = 0;
  virtual int raw_stat_sync_to_global(RecRawStatBlock *rsb, int id)               = 0;
  virtual int raw_stat_clear(RecRawStatBlock *rsb, int id)                        = 0;
};

// WARNING!  It's advised that developers do not modify the contents of
// the RecRawStatBlock.  ^_^
struct RecRawStatBlock {
  off_t ethr_stat_offset; // thread local raw-stat storage
  RecRawStat **global;    // global raw-stat storage (ptr to RecRecord)
  int num_stats;          // number of stats in this block
  int max_stats;          // maximum number of stats for this block
  ink_mutex mutex;
  RecRawStatBlockOps *ops;
};

//-------------------------------------------------------------------------
// RecCore Callback Types
//-------------------------------------------------------------------------

using RecConfigUpdateCb = std::function<int(const char *, RecDataT, RecData, void *)>;
using RecStatUpdateFunc = int (*)(const char *, RecDataT, RecData *, RecRawStatBlock *, int, void *);
using RecRawStatSyncCb  = int (*)(const char *, RecDataT, RecData *, RecRawStatBlock *, int);
using RecContextCb      = bool(const char *, RecDataT, RecData, void *);
