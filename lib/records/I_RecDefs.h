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

#ifndef _I_REC_DEFS_H_
#define _I_REC_DEFS_H_

#include "ts/ink_mutex.h"
#include "ts/ink_rwlock.h"
#include "I_RecMutex.h"

#define STAT_PROCESSOR

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
#define RecStringNull NULL

typedef int64_t RecInt;
typedef float RecFloat;
typedef char *RecString;
typedef const char *RecStringConst;
typedef int64_t RecCounter;
typedef int8_t RecByte;
typedef bool RecBool;

enum RecT {
  RECT_NULL    = 0x00,
  RECT_CONFIG  = 0x01,
  RECT_PROCESS = 0x02,
  RECT_NODE    = 0x04,
  RECT_CLUSTER = 0x08,
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

#if defined(STAT_PROCESSOR)
  RECD_CONST, // Added for the StatProcessor, store as RECD_FLOAT
  RECD_FX,    // Added for the StatProcessor, store as RECD_INT
#endif
  RECD_MAX
};

enum RecPersistT {
  RECP_NULL,
  RECP_PERSISTENT,
  RECP_NON_PERSISTENT,
};

// RECP_NULL should never be used by callers of RecRegisterStat*(). You have to decide
// whether to persist stats or not. The template goop below make sure that passing RECP_NULL
// is a very ugle compile-time error.

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
}
}

#define REC_PERSISTENCE_TYPE(P) rec::detail::is_valid_persistence<P>::value

enum RecUpdateT {
  RECU_NULL,       // default: don't know the behavior
  RECU_DYNAMIC,    // config can be updated dynamically w/ "traffic_ctl config reload"
  RECU_RESTART_TS, // config requires TS to be restarted to take effect
  RECU_RESTART_TM, // config requires TM/TS to be restarted to take effect
  RECU_RESTART_TC  // config requires TC/TM/TS to be restarted to take effect
};

enum RecCheckT {
  RECC_NULL, // default: no check type defined
  RECC_STR,  // config is a string
  RECC_INT,  // config is an integer with a range
  RECC_IP    // config is an ip address
};

/// The source of the value.
/// @internal @c REC_SOURCE_NULL is useful for a return value, I don't see using it in the actual data.
enum RecSourceT {
  REC_SOURCE_NULL,     ///< No source / value not set.
  REC_SOURCE_DEFAULT,  ///< Built in default.
  REC_SOURCE_EXPLICIT, ///< Set by administrator (config file, external API, cluster, etc.)
  REC_SOURCE_ENV       ///< Process environment variable.
};

enum RecModeT {
  RECM_NULL,
  RECM_CLIENT,
  RECM_SERVER,
  RECM_STAND_ALONE,
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
  // XXX - these will waist some space because they are only needed for the globals
  // this is a fix for bug TS-162, so I am trying to do as few code changes as
  // possible, this should be revisted -bcall
  int64_t last_sum;   // value from the last global sync
  int64_t last_count; // value from the last global sync
  uint32_t version;
};

// WARNING!  It's advised that developers do not modify the contents of
// the RecRawStatBlock.  ^_^
struct RecRawStatBlock {
  off_t ethr_stat_offset; // thread local raw-stat storage
  RecRawStat **global;    // global raw-stat storage (ptr to RecRecord)
  int num_stats;          // number of stats in this block
  int max_stats;          // maximum number of stats for this block
  ink_mutex mutex;
};

//-------------------------------------------------------------------------
// RecCore Callback Types
//-------------------------------------------------------------------------
typedef int (*RecConfigUpdateCb)(const char *name, RecDataT data_type, RecData data, void *cookie);
typedef int (*RecStatUpdateFunc)(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id, void *cookie);
typedef int (*RecRawStatSyncCb)(const char *name, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id);

#endif
