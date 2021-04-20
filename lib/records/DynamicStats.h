/** @file

  Dynamic Stats

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

#include "records/I_RecCore.h"
#include "records/I_RecDefs.h"
#include "records/I_RecProcess.h"

/**
   @class DynamicStats
   @details
     core version of TSStat* APIs

     - supported: TSStatCreate, TSStatIntIncrement/TSStatIntDecrement, TSStatIntGet, TSStatIntSet, TSStatFindName

   CAVEAT: The librecords doesn't have APIs for "unregister stats" or "realloc rsb".
   If you want to register ton of stats, bump -maxRecords option of traffic_manager/traffic_server.

 */
class DynamicStats
{
public:
  // Modifiers
  void init(int max_stats);
  int create(RecT rec_type, const char *name, RecDataT data_type, RecRawStatSyncCb sync_cb, bool is_persistent = false);
  int increment(int id, int64_t amount);
  int set_sum(int id, int64_t value);

  // References
  int64_t get_sum(int id) const;
  int find(const char *name) const;
  bool is_allocated() const;

private:
  RecRawStatBlock *_rsb       = nullptr;
  std::atomic<int> _rsb_index = 0;
};

////
// Inline functions
//
inline void
DynamicStats::init(int max_stats)
{
  _rsb = RecAllocateRawStatBlock(max_stats);
}

/**
   TSStatCreate
 */
inline int
DynamicStats::create(RecT rec_type, const char *name, RecDataT data_type, RecRawStatSyncCb sync_cb, bool is_persistent)
{
  if (name == nullptr || _rsb_index >= _rsb->max_stats) {
    return REC_ERR_FAIL;
  }

  int stat_id = _rsb_index;

  if (is_persistent) {
    int r = RecRegisterRawStat(_rsb, rec_type, name, data_type, RECP_PERSISTENT, stat_id, sync_cb);
    if (r != REC_ERR_OKAY) {
      return REC_ERR_FAIL;
    }
  } else {
    int r = RecRegisterRawStat(_rsb, rec_type, name, data_type, RECP_NON_PERSISTENT, stat_id, sync_cb);
    if (r != REC_ERR_OKAY) {
      return REC_ERR_FAIL;
    }
  }

  RecSetRawStatSum(_rsb, stat_id, 0);
  RecSetRawStatCount(_rsb, stat_id, 0);

  _rsb_index++;

  return stat_id;
}

/**
   TSStatIntIncrement / TSStatIntDecrement
 */
inline int
DynamicStats::increment(int id, int64_t amount)
{
  if (id < 0) {
    return REC_ERR_FAIL;
  }

  return RecIncrRawStat(_rsb, nullptr, id, amount);
}

/**
   TSStatIntSet
 */
inline int
DynamicStats::set_sum(int id, int64_t value)
{
  if (id < 0) {
    return REC_ERR_FAIL;
  }

  return RecSetGlobalRawStatSum(_rsb, id, value);
}

/**
   TSStatIntGet
 */
inline int64_t
DynamicStats::get_sum(int id) const
{
  int64_t value = -1;

  if (id < 0) {
    return value;
  }

  RecGetGlobalRawStatSum(_rsb, id, &value);

  return value;
}

/**
   TSStatFindName
 */
inline int
DynamicStats::find(const char *name) const
{
  if (name == nullptr) {
    return REC_ERR_FAIL;
  }

  int id;
  if (RecGetRecordOrderAndId(name, nullptr, &id, true, true) != REC_ERR_OKAY) {
    return REC_ERR_FAIL;
  }

  if (RecGetGlobalRawStatPtr(_rsb, id) == nullptr) {
    return REC_ERR_FAIL;
  }

  return id;
}

inline bool
DynamicStats::is_allocated() const
{
  return _rsb != nullptr;
}
