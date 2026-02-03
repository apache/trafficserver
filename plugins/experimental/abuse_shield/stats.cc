/** @file

  Abuse Shield plugin statistics implementation.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements.  See the NOTICE file distributed with this work for additional information regarding
  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the License.  You may obtain
  a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

#include "stats.h"

#include <string>

#include "ts/ts.h"

namespace abuse_shield
{

void
TrackerStats::init(const char *prefix)
{
  std::string base = std::string("abuse_shield.") + prefix + ".";

  events       = TSStatCreate((base + "events").c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
  slots_used   = TSStatCreate((base + "slots_used").c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  contests     = TSStatCreate((base + "contests").c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  contests_won = TSStatCreate((base + "contests_won").c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  evictions    = TSStatCreate((base + "evictions").c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
}

void
ActionStats::init()
{
  rules_matched   = TSStatCreate("abuse_shield.rules.matched", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
  actions_blocked = TSStatCreate("abuse_shield.actions.blocked", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
  actions_closed  = TSStatCreate("abuse_shield.actions.closed", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
  actions_logged  = TSStatCreate("abuse_shield.actions.logged", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
  connections_rejected =
    TSStatCreate("abuse_shield.connections.rejected", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
}

} // namespace abuse_shield
