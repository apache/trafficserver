/** @file

  Render records in the Prometheus text format from a cache of translated names.

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

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ts/apidefs.h>

enum class PrometheusType : uint8_t { COUNTER, GAUGE, UNTYPED };

/// The Prometheus form of a record name.  An empty family leaves the record out of the output.
struct PrometheusName {
  std::string    family;
  std::string    labels; ///< The label pairs without the braces, for example @c method="get".
  PrometheusType type = PrometheusType::UNTYPED;
};

/// Translates the name of a counter, integer or float record.
using PrometheusNamer = PrometheusName (*)(std::string_view name, TSRecordDataType data_type);

struct PrometheusOptions {
  PrometheusNamer namer         = nullptr;
  bool            help          = true; ///< Write a HELP line, with the record name, for each family.
  bool            wrap_counters = false;
};

/** Renders records in the Prometheus text format.
 *
 * The renderer translates each record name once and keeps the result, so that a render only formats the values.  The
 * samples of a family appear together, under one TYPE line, in the order in which the renderer first saw them.  The
 * first record of a family sets its type and its HELP text.  A record that is missing from a render leaves out only its
 * own sample, and a new record joins its family.
 *
 * A renderer is not thread safe.
 */
class PrometheusRenderer
{
public:
  explicit PrometheusRenderer(const PrometheusOptions &options) : _options(options) {}

  const PrometheusOptions &
  options() const
  {
    return _options;
  }

  /// Start a render.  Pass each record to add(), in the order of TSRecordDump, and then call render().
  void begin();

  /** Add a record to the render.
   *
   * The renderer identifies a record by the address of @a name, so the name must not change or be freed while the
   * renderer exists.  The names that TSRecordDump passes meet this.
   */
  void add(const char *name, TSRecordDataType data_type, const TSRecordData &datum);

  /// The samples of the records added since begin().  The caller can append to the buffer.  The next render reuses it.
  std::string &render();

private:
  struct Family {
    std::string           header; ///< The HELP and TYPE lines.
    PrometheusType        type = PrometheusType::UNTYPED;
    std::vector<uint32_t> members; ///< Indexes into _records.
  };

  struct Record {
    TSRecordDataType data_type   = TS_RECORDDATATYPE_NULL;
    uint64_t         last_render = 0;
    std::string      prefix; ///< The sample up to its value, for example @c name{method="get"} with a trailing space.
    TSRecordData     datum{};
  };

  // The record at one position of the dump.
  struct Slot {
    const char *name;
    uint32_t    record;
  };

  uint32_t find(const char *name, TSRecordDataType data_type);
  uint32_t translate(std::string_view name, TSRecordDataType data_type);
  void     append_value(const Record &record);

  PrometheusOptions                          _options;
  std::vector<Record>                        _records;
  std::vector<Family>                        _families;
  std::unordered_map<std::string, uint32_t>  _family_index;
  std::vector<Slot>                          _slots;
  std::unordered_map<const char *, uint32_t> _by_pointer;
  std::unordered_map<std::string, uint32_t>  _by_name;
  uint64_t                                   _renders  = 0;
  uint32_t                                   _position = 0;
  std::string                                _body;
};
