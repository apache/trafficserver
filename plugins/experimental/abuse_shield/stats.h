/** @file

  Abuse Shield plugin statistics.

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

#pragma once

namespace abuse_shield
{

/** Per-tracker statistics/metrics. */
struct TrackerStats {
  int events{-1};       ///< Total events processed (record_event calls)
  int slots_used{-1};   ///< Current slots in use (gauge)
  int contests{-1};     ///< Total contest attempts
  int contests_won{-1}; ///< Contests won by new IP
  int evictions{-1};    ///< IPs evicted (score reached 0)

  /** Initialize tracker statistics with a name prefix.
   *
   * @param[in] prefix The prefix for stat names (e.g., "txn", "conn", "h2").
   */
  void init(const char *prefix);
};

/** Global action statistics/metrics. */
struct ActionStats {
  int rules_matched{-1};        ///< Total times any rule matched
  int actions_blocked{-1};      ///< Total block actions executed
  int actions_closed{-1};       ///< Total close actions executed
  int actions_logged{-1};       ///< Total log actions executed
  int connections_rejected{-1}; ///< Connections rejected at VCONN_START (blocked IPs, HTTP & HTTPS)

  /** Initialize action statistics by creating the ATS stat entries. */
  void init();
};

} // namespace abuse_shield
