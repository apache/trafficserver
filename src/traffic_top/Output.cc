/** @file

    Output formatters implementation for traffic_top batch mode.

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

#include "Output.h"

#include <ctime>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace traffic_top
{

Output::Output(OutputFormat format, FILE *output_file) : _format(format), _output(output_file)
{
  // Use default summary stats if none specified
  if (_stat_keys.empty()) {
    _stat_keys = getDefaultSummaryKeys();
  }
}

std::vector<std::string>
getDefaultSummaryKeys()
{
  return {
    "client_req",       // Requests per second
    "ram_ratio",        // RAM cache hit rate
    "fresh",            // Fresh hit %
    "cold",             // Cold miss %
    "client_curr_conn", // Current connections
    "disk_used",        // Disk cache used
    "client_net",       // Client bandwidth
    "server_req",       // Origin requests/sec
    "200",              // 200 responses %
    "5xx"               // 5xx errors %
  };
}

std::vector<std::string>
getAllStatKeys(Stats &stats)
{
  return stats.getStatKeys();
}

std::string
Output::getCurrentTimestamp() const
{
  time_t    now = time(nullptr);
  struct tm nowtm;
  char      buf[32];

  localtime_r(&now, &nowtm);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &nowtm);
  return std::string(buf);
}

std::string
Output::formatValue(double value, StatType type) const
{
  std::ostringstream oss;

  if (isPercentage(type)) {
    oss << std::fixed << std::setprecision(1) << value;
  } else if (value >= 1000000000000.0) {
    oss << std::fixed << std::setprecision(1) << (value / 1000000000000.0) << "T";
  } else if (value >= 1000000000.0) {
    oss << std::fixed << std::setprecision(1) << (value / 1000000000.0) << "G";
  } else if (value >= 1000000.0) {
    oss << std::fixed << std::setprecision(1) << (value / 1000000.0) << "M";
  } else if (value >= 1000.0) {
    oss << std::fixed << std::setprecision(1) << (value / 1000.0) << "K";
  } else {
    oss << std::fixed << std::setprecision(1) << value;
  }

  return oss.str();
}

void
Output::printHeader()
{
  if (_format == OutputFormat::Text && _print_header && !_header_printed) {
    printTextHeader();
    _header_printed = true;
  }
}

void
Output::printTextHeader()
{
  // Print column headers
  if (_include_timestamp) {
    fprintf(_output, "%-20s", "TIMESTAMP");
  }

  for (const auto &key : _stat_keys) {
    // Get pretty name from stats (we need a Stats instance for this)
    // For header, use the key name abbreviated
    std::string header = key;
    if (header.length() > 10) {
      header = header.substr(0, 9) + ".";
    }
    fprintf(_output, "%12s", header.c_str());
  }
  fprintf(_output, "\n");

  // Print separator line
  if (_include_timestamp) {
    fprintf(_output, "--------------------");
  }
  for (size_t i = 0; i < _stat_keys.size(); ++i) {
    fprintf(_output, "------------");
  }
  fprintf(_output, "\n");

  fflush(_output);
}

void
Output::printStats(Stats &stats)
{
  if (_format == OutputFormat::Text) {
    printHeader();
    printTextStats(stats);
  } else {
    printJsonStats(stats);
  }
}

void
Output::printTextStats(Stats &stats)
{
  // Timestamp
  if (_include_timestamp) {
    fprintf(_output, "%-20s", getCurrentTimestamp().c_str());
  }

  // Values
  for (const auto &key : _stat_keys) {
    double      value = 0;
    std::string prettyName;
    StatType    type;

    if (stats.hasStat(key)) {
      stats.getStat(key, value, prettyName, type);
      std::string formatted = formatValue(value, type);

      if (isPercentage(type)) {
        fprintf(_output, "%11s%%", formatted.c_str());
      } else {
        fprintf(_output, "%12s", formatted.c_str());
      }
    } else {
      fprintf(_output, "%12s", "N/A");
    }
  }

  fprintf(_output, "\n");
  fflush(_output);
}

void
Output::printJsonStats(Stats &stats)
{
  fprintf(_output, "{");

  bool first = true;

  // Timestamp
  if (_include_timestamp) {
    fprintf(_output, "\"timestamp\":\"%s\"", getCurrentTimestamp().c_str());
    first = false;
  }

  // Host
  if (!first) {
    fprintf(_output, ",");
  }
  fprintf(_output, "\"host\":\"%s\"", stats.getHost().c_str());
  first = false;

  // Stats values
  for (const auto &key : _stat_keys) {
    double      value = 0;
    std::string prettyName;
    StatType    type;

    if (stats.hasStat(key)) {
      stats.getStat(key, value, prettyName, type);

      if (!first) {
        fprintf(_output, ",");
      }

      // Use key name as JSON field
      // Check for NaN or Inf
      if (std::isnan(value) || std::isinf(value)) {
        fprintf(_output, "\"%s\":null", key.c_str());
      } else {
        fprintf(_output, "\"%s\":%.2f", key.c_str(), value);
      }
      first = false;
    }
  }

  fprintf(_output, "}\n");
  fflush(_output);
}

void
Output::printError(const std::string &message)
{
  if (_format == OutputFormat::Json) {
    fprintf(_output, "{\"error\":\"%s\",\"timestamp\":\"%s\"}\n", message.c_str(), getCurrentTimestamp().c_str());
  } else {
    fprintf(stderr, "Error: %s\n", message.c_str());
  }
  fflush(_output);
}

} // namespace traffic_top
