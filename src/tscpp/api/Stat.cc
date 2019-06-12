/**
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
/**
 * @file Stat.cc
 */

#include "tscpp/api/Stat.h"
#include <string>
#include <cstdint>
#include "ts/ts.h"
#include "logging_internal.h"

using namespace atscppapi;
using std::string;

Stat::Stat()
{
  // ATS Guarantees that stat ids will always be > 0. So we can use stat_id_ > 0 to
  // verify that this stat has been properly initialized.
}

Stat::~Stat()
{
  // we really dont have any cleanup since ATS doesn't expose a method to destroy stats
}

bool
Stat::init(const string &name, Stat::SyncType type, bool persistent)
{
  if (TSStatFindName(name.c_str(), &stat_id_) == TS_SUCCESS) {
    LOG_DEBUG("Attached to stat '%s' with stat_id = %d", name.c_str(), stat_id_);
    return true;
  }

  // TS_RECORDDATATYPE_INT is the only type currently supported
  // so that's why this api doesn't expose other types, TSStatSync is equivalent to StatSyncType
  stat_id_ = TSStatCreate(name.c_str(), TS_RECORDDATATYPE_INT, persistent ? TS_STAT_PERSISTENT : TS_STAT_NON_PERSISTENT,
                          static_cast<TSStatSync>(type));
  if (stat_id_ != TS_ERROR) {
    LOG_DEBUG("Created new stat named '%s' with stat_id = %d", name.c_str(), stat_id_);
  } else {
    LOG_ERROR("Unable to create stat named '%s'.", name.c_str());
  }

  if (stat_id_ == TS_ERROR) {
    return false;
  }

  if (!persistent) {
    set(0);
  }

  return true;
}

void
Stat::set(int64_t value)
{
  if (stat_id_ == TS_ERROR) {
    return;
  }

  TSStatIntSet(stat_id_, value);
}

int64_t
Stat::get() const
{
  if (stat_id_ == TS_ERROR) {
    return 0;
  }

  return TSStatIntGet(stat_id_);
}

void
Stat::increment(int64_t amount)
{
  if (stat_id_ == TS_ERROR) {
    return;
  }

  TSStatIntIncrement(stat_id_, amount);
}

void
Stat::decrement(int64_t amount)
{
  if (stat_id_ == TS_ERROR) {
    return;
  }

  TSStatIntDecrement(stat_id_, amount);
}
