/** @file

    Output formatters for traffic_top batch mode.

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

#include <cstdio>
#include <string>
#include <vector>

#include "Stats.h"

namespace traffic_top
{

/// Output format types
enum class OutputFormat { Text, Json };

/**
 * Output formatter for batch mode.
 *
 * Supports vmstat-style text output and JSON output for
 * machine consumption.
 */
class Output
{
public:
  /**
   * Constructor.
   * @param format Output format (text or JSON)
   * @param output_file File handle to write to (defaults to stdout)
   */
  explicit Output(OutputFormat format, FILE *output_file = stdout);
  ~Output() = default;

  // Non-copyable
  Output(const Output &)            = delete;
  Output &operator=(const Output &) = delete;

  /**
   * Set custom stat keys to output.
   * If not set, uses default summary stats.
   */
  void
  setStatKeys(const std::vector<std::string> &keys)
  {
    _stat_keys = keys;
  }

  /**
   * Print the header line (for text format).
   * Called once before the first data line.
   */
  void printHeader();

  /**
   * Print a data line with current stats.
   * @param stats Stats object with current values
   */
  void printStats(Stats &stats);

  /**
   * Print an error message.
   * @param message Error message to print
   */
  void printError(const std::string &message);

  /**
   * Set whether to include timestamp in output.
   */
  void
  setIncludeTimestamp(bool include)
  {
    _include_timestamp = include;
  }

  /**
   * Set whether to print header.
   */
  void
  setPrintHeader(bool print)
  {
    _print_header = print;
  }

  /**
   * Get the output format.
   */
  OutputFormat
  getFormat() const
  {
    return _format;
  }

private:
  void printTextHeader();
  void printTextStats(Stats &stats);
  void printJsonStats(Stats &stats);

  std::string formatValue(double value, StatType type) const;
  std::string getCurrentTimestamp() const;

  OutputFormat             _format;
  FILE                    *_output;
  std::vector<std::string> _stat_keys;
  bool                     _include_timestamp = true;
  bool                     _print_header      = true;
  bool                     _header_printed    = false;
};

/**
 * Get default stat keys for summary output.
 */
std::vector<std::string> getDefaultSummaryKeys();

/**
 * Get all stat keys for full output.
 */
std::vector<std::string> getAllStatKeys(Stats &stats);

} // namespace traffic_top
