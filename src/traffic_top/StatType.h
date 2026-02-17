/** @file

    StatType enum for traffic_top statistics.

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

namespace traffic_top
{

/**
 * Enumeration of statistic types used for display and calculation.
 *
 * Each type determines how a statistic value is fetched, calculated, and displayed.
 */
enum class StatType {
  Absolute    = 1, ///< Absolute value, displayed as-is (e.g., disk used, current connections)
  Rate        = 2, ///< Rate per second, calculated from delta over time interval
  Ratio       = 3, ///< Ratio of two stats (numerator / denominator)
  Percentage  = 4, ///< Percentage (ratio * 100, displayed with % suffix)
  RequestPct  = 5, ///< Percentage of client requests (value / client_req * 100)
  Sum         = 6, ///< Sum of two rate stats
  SumBits     = 7, ///< Sum of two rate stats * 8 (bytes to bits conversion)
  TimeRatio   = 8, ///< Time ratio in milliseconds (totaltime / count)
  SumAbsolute = 9, ///< Sum of two absolute stats
  RateNsToMs  = 10 ///< Rate in nanoseconds, converted to milliseconds (divide by 1,000,000)
};

/**
 * Convert StatType enum to its underlying integer value.
 */
inline int
toInt(StatType type)
{
  return static_cast<int>(type);
}

/**
 * Check if this stat type represents a percentage value.
 */
inline bool
isPercentage(StatType type)
{
  return type == StatType::Percentage || type == StatType::RequestPct;
}

/**
 * Check if this stat type needs the previous stats for rate calculation.
 */
inline bool
needsPreviousStats(StatType type)
{
  return type == StatType::Rate || type == StatType::RequestPct || type == StatType::TimeRatio || type == StatType::RateNsToMs;
}

} // namespace traffic_top
