/** @file

    Stats class declaration for traffic_top.

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

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <sys/time.h>
#include <vector>

#include "StatType.h"

namespace traffic_top
{

/**
 * Defines a statistic lookup item with display name, metric name(s), and type.
 */
struct LookupItem {
  /// Constructor for simple stats that map directly to a metric
  LookupItem(const char *pretty_name, const char *metric_name, StatType stat_type)
    : pretty(pretty_name), name(metric_name), numerator(""), denominator(""), type(stat_type)
  {
  }

  /// Constructor for derived stats that combine two metrics
  LookupItem(const char *pretty_name, const char *num, const char *denom, StatType stat_type)
    : pretty(pretty_name), name(num), numerator(num), denominator(denom), type(stat_type)
  {
  }

  const char *pretty;      ///< Display name shown in UI
  const char *name;        ///< Primary metric name or numerator reference
  const char *numerator;   ///< Numerator stat key (for derived stats)
  const char *denominator; ///< Denominator stat key (for derived stats)
  StatType    type;        ///< How to calculate and display this stat
};

/**
 * Stats collector and calculator for traffic_top.
 *
 * Fetches statistics from ATS via RPC and provides methods to
 * retrieve calculated values for display.
 */
class Stats
{
public:
  Stats();
  ~Stats() = default;

  // Non-copyable, non-movable
  Stats(const Stats &)            = delete;
  Stats &operator=(const Stats &) = delete;
  Stats(Stats &&)                 = delete;
  Stats &operator=(Stats &&)      = delete;

  /**
   * Fetch latest stats from the ATS RPC interface.
   * @return true on success, false on error
   */
  bool getStats();

  /**
   * Get last error message from stats fetch.
   * @return Error message or empty string if no error
   */
  const std::string &
  getLastError() const
  {
    return _last_error;
  }

  /**
   * Get a stat value by key.
   * @param key The stat key from the lookup table
   * @param value Output: the calculated value
   * @param overrideType Optional type override for calculation
   */
  void getStat(const std::string &key, double &value, StatType overrideType = StatType::Absolute);

  /**
   * Get a stat value with metadata.
   * @param key The stat key from the lookup table
   * @param value Output: the calculated value
   * @param prettyName Output: the display name
   * @param type Output: the stat type
   * @param overrideType Optional type override for calculation
   */
  void getStat(const std::string &key, double &value, std::string &prettyName, StatType &type,
               StatType overrideType = StatType::Absolute);

  /**
   * Get a string stat value (e.g., version).
   * @param key The stat key
   * @param value Output: the string value
   */
  void getStat(const std::string &key, std::string &value);

  /**
   * Toggle between absolute and rate display mode.
   * @return New absolute mode state
   */
  bool toggleAbsolute();

  /**
   * Set absolute display mode.
   */
  void
  setAbsolute(bool absolute)
  {
    _absolute = absolute;
  }

  /**
   * Check if currently in absolute display mode.
   */
  bool
  isAbsolute() const
  {
    return _absolute;
  }

  /**
   * Check if we can calculate rates (have previous stats).
   */
  bool
  canCalculateRates() const
  {
    return _old_stats != nullptr && _time_diff > 0;
  }

  /**
   * Get the hostname.
   */
  const std::string &
  getHost() const
  {
    return _host;
  }

  /**
   * Get the time difference since last stats fetch (seconds).
   */
  double
  getTimeDiff() const
  {
    return _time_diff;
  }

  /**
   * Get all available stat keys.
   */
  std::vector<std::string> getStatKeys() const;

  /**
   * Check if a stat key exists.
   */
  bool hasStat(const std::string &key) const;

  /**
   * Get the lookup item for a stat key.
   */
  const LookupItem *getLookupItem(const std::string &key) const;

  /**
   * Get history data for a stat, normalized to 0.0-1.0 range.
   * @param key The stat key
   * @param maxValue The maximum value for normalization (0 = auto-scale)
   * @return Vector of normalized values (oldest to newest)
   */
  std::vector<double> getHistory(const std::string &key, double maxValue = 0.0) const;

  /**
   * Get the maximum history length.
   */
  static constexpr size_t
  getMaxHistoryLength()
  {
    return MAX_HISTORY_LENGTH;
  }

  /**
   * Validate the lookup table for internal consistency.
   * Checks that derived stats (Ratio, Percentage, Sum, etc.) reference
   * valid numerator and denominator keys.
   * @return Number of validation errors found (0 = all valid)
   */
  int validateLookupTable() const;

private:
  // Maximum number of historical data points to store for graphs
  // At 5 second intervals, 120 points = 10 minutes of history
  static constexpr size_t MAX_HISTORY_LENGTH = 120;

  /**
   * Get raw metric value from the stats map.
   * @param key The ATS metric name (e.g., "proxy.process.http.incoming_requests")
   * @param stats Pointer to the stats map (current or old)
   * @return The metric value as int64_t, or 0 if not found
   */
  int64_t getValue(const std::string &key, const std::map<std::string, std::string> *stats) const;

  /**
   * Fetch all metrics from ATS via JSON-RPC and populate the stats map.
   * @param lookup_table The lookup table defining which metrics to fetch
   * @param stats Output map to populate with metric name -> value pairs
   * @return Empty string on success, error message on failure
   */
  std::string fetch_and_fill_stats(const std::map<std::string, LookupItem> &lookup_table,
                                   std::map<std::string, std::string>      *stats);

  /**
   * Initialize the lookup table with all stat definitions.
   * This defines the mapping from display keys (e.g., "client_req") to
   * ATS metrics (e.g., "proxy.process.http.incoming_requests") and
   * how to calculate/display each stat.
   */
  void initializeLookupTable();

  // -------------------------------------------------------------------------
  // Stats storage
  // -------------------------------------------------------------------------
  // We keep two snapshots of stats to calculate rates (delta / time_diff)
  std::unique_ptr<std::map<std::string, std::string>> _stats;     ///< Current stats snapshot
  std::unique_ptr<std::map<std::string, std::string>> _old_stats; ///< Previous stats snapshot

  // -------------------------------------------------------------------------
  // Configuration and metadata
  // -------------------------------------------------------------------------
  std::map<std::string, LookupItem>         _lookup_table; ///< Stat key -> metric mapping
  std::map<std::string, std::deque<double>> _history;      ///< Historical values for graphs
  std::string                               _host;         ///< Hostname for display
  std::string                               _last_error;   ///< Last error message from RPC

  // -------------------------------------------------------------------------
  // Timing for rate calculations
  // -------------------------------------------------------------------------
  double         _old_time  = 0;      ///< Timestamp of previous stats fetch (seconds)
  double         _now       = 0;      ///< Timestamp of current stats fetch (seconds)
  double         _time_diff = 0;      ///< Time between fetches (for rate calculation)
  struct timeval _time      = {0, 0}; ///< Raw timeval from gettimeofday()

  // -------------------------------------------------------------------------
  // Display mode
  // -------------------------------------------------------------------------
  bool _absolute = true; ///< True = show absolute values, False = show rates
};

} // namespace traffic_top
