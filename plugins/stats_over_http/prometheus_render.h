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
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ts/apidefs.h>

class PrometheusRules;

enum class PrometheusType : uint8_t { COUNTER, GAUGE, UNTYPED };

std::string_view prometheus_type_name(PrometheusType type);

/// Appends @a value to @a out with the escapes of a label value.
void prometheus_escape_label_value(std::string &out, std::string_view value);

/// Appends @a text to @a out with the escapes of HELP text.
void prometheus_escape_help_text(std::string &out, std::string_view text);

/// The Prometheus form of a record name.  An empty family leaves the record out of the output.
struct PrometheusName {
  std::string    family;
  std::string    labels; ///< The label pairs without the braces, for example @c method="get".
  PrometheusType type = PrometheusType::UNTYPED;
};

/// Translates the name of a counter, integer or float record.
using PrometheusNamer = PrometheusName (*)(std::string_view name, TSRecordDataType data_type);

struct PrometheusOptions {
  PrometheusNamer namer = nullptr;
  /// When set, the rules name the records instead of @a namer.
  std::shared_ptr<const PrometheusRules> rules;
  bool                                   help          = true; ///< Write a HELP line, with the record name, for each family.
  bool                                   wrap_counters = false;
  /// Reports a record that the output leaves out or changes, once for each family and kind of change.
  void (*warn)(const std::string &message) = nullptr;
};

/// What the last render wrote and left out.
struct PrometheusRenderStats {
  uint64_t series         = 0; ///< Samples written.
  uint64_t dropped        = 0; ///< Samples left out because of their label names, their kind, their name or the series limit.
  uint64_t relabeled      = 0; ///< Samples written with the label names of their family rather than their own.
  uint64_t duplicates     = 0; ///< Samples left out because an earlier record has the same series.
  uint64_t type_conflicts = 0; ///< Samples of another type than their family.
};

/** Renders records in the Prometheus text format.
 *
 * The renderer translates each record name once and keeps the result, so that a render only formats the values.  The
 * samples of a family appear together, under one TYPE line.  Without rules, the families and their samples appear in the
 * order in which the renderer first saw them.  With rules, the families appear in the order of their names, and the
 * samples of a family in the order of their labels without the string label, so that the order of the output does not
 * depend on the order of the records.  The first record of a family sets its type and its HELP text, even a record that
 * the family leaves out.  A record that is missing from a render leaves out only its own sample, and a new record joins
 * its family.
 *
 * With rules, the record whose rule comes last sets the label names of its family.  A record that matches no rule counts
 * as after the last rule.  A render leaves out a sample with another number of labels.  A sample with the same label
 * names in another order keeps its values, and a sample with as many labels but other names gets the names of the
 * family.  Of the records in a render that have the same series, the one that the renderer saw first writes it.
 *
 * A TEXT_LABEL record, a string record in strings.names, writes its string in a label.  The string is its value, so it is
 * not a part of the key of its series: two TEXT_LABEL records with the same family and other labels are duplicates,
 * whatever their strings.  The samples of a family are all of TEXT_LABEL records, or all of other records.  The record
 * that sets the label names of the family decides which.  At the same rule, a record that is not TEXT_LABEL sets the
 * label names rather than a TEXT_LABEL record, so that the kind of a family does not depend on the order of the records.
 * A render leaves out a sample of the other kind.
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

  const PrometheusRenderStats &
  stats() const
  {
    return _stats;
  }

private:
  enum class Value : uint8_t {
    NONE,        ///< The record has no sample.
    NUMBER,      ///< The value of a counter, integer or float record.
    TEXT_LABEL,  ///< 1, with the string value of the record in a label.
    TEXT_NUMBER, ///< The string value of the record, when it is a number.
  };

  // The kinds of change that a family reports once.
  enum Warning : uint8_t { WARN_LABELS = 1, WARN_RELABELED = 2, WARN_DUPLICATE = 4, WARN_TYPE = 8, WARN_KIND = 16 };

  struct Family {
    std::string           header; ///< The HELP and TYPE lines.
    PrometheusType        type = PrometheusType::UNTYPED;
    std::vector<uint32_t> members; ///< Indexes into _records.
  };

  // Which record writes the series of a record, at the same index as its Record.
  struct Writer {
    uint32_t first   = 0; ///< The first record with the same series, which writes the series.
    uint64_t written = 0; ///< The last render that wrote the series.  Only the Writer of the first record uses it.
  };

  // What the rules need of a family, at the same index as its Family.
  struct FamilyInfo {
    std::string name;
    uint32_t    definer  = 0;     ///< The member that sets the label names of the family.
    uint8_t     warned   = 0;     ///< The Warning bits that the family has reported.
    bool        unsorted = false; ///< A member joined the family or changed its labels since the last sort.
  };

  // What a render reads of a record.  Translation and the rules keep the rest in a RecordInfo, so that a render reads
  // fewer cache lines.
  struct Record {
    TSRecordDataType data_type   = TS_RECORDDATATYPE_NULL;
    Value            value       = Value::NONE;
    PrometheusType   type        = PrometheusType::UNTYPED;
    bool             dropped     = false; ///< Its labels differ in number from its family, or it is of the other kind.
    bool             relabeled   = false;
    uint64_t         last_render = 0;
    TSRecordData     datum{};
    /// The sample up to its value, for example @c name{method="get"} with a trailing space.  A TEXT_LABEL record has an
    /// empty string label here, at RecordInfo::split.
    std::string prefix;
  };

  // The rest of a record, at the same index as its Record.
  struct RecordInfo {
    std::string_view                name; ///< The key of the record in _by_name.
    const std::vector<std::string> *label_names = nullptr;
    uint32_t                        family      = UINT32_MAX;
    uint32_t                        rule_index  = 0;
    uint32_t                        split       = 0;     ///< Where the string value goes in the prefix of a TEXT_LABEL record.
    bool                            invalid     = false; ///< The rules give the record an invalid metric name.
    std::string                     text;                ///< The string value of a TEXT_LABEL or TEXT_NUMBER record.
    double                          number = 0;          ///< The value of a TEXT_NUMBER record.
  };

  // The record at one position of the dump.
  struct Slot {
    const char *name;
    uint32_t    record;
  };

  uint32_t find(const char *name, TSRecordDataType data_type);
  uint32_t add_record(std::string_view name, TSRecordDataType data_type);
  uint32_t translate(std::string_view name, TSRecordDataType data_type);
  uint32_t translate_rules(std::string_view name, TSRecordDataType data_type);
  uint32_t add_family(const std::string &name, std::string_view record_name, PrometheusType type, uint32_t record);
  void     place(uint32_t index, const std::vector<std::string> &label_values);
  void     regroup(uint32_t family);
  void     mark_unsorted(uint32_t family);
  void     sort_output();
  void     forget_series(uint32_t index);
  bool     defines_over(uint32_t a, uint32_t b) const;
  bool     other_kind(uint32_t index) const;
  void     report(uint32_t index, Warning warning);
  bool     take(uint32_t index);
  void     write(const Family &family, uint32_t index, bool &header);
  void     append_value(const Record &record);

  PrometheusOptions                          _options;
  std::vector<Record>                        _records;
  std::vector<RecordInfo>                    _info;
  std::vector<Writer>                        _writers;
  std::vector<Family>                        _families;
  std::vector<FamilyInfo>                    _family_info;
  std::unordered_map<std::string, uint32_t>  _family_index;
  std::vector<Slot>                          _slots;
  std::unordered_map<const char *, uint32_t> _by_pointer;
  std::unordered_map<std::string, uint32_t>  _by_name;
  std::unordered_multimap<size_t, uint32_t>  _series; ///< The first record of each series, by the hash of its prefix.
  std::vector<uint32_t>                      _selected;
  std::vector<uint32_t>                      _order;    ///< With rules, the families in the order of their names.
  std::vector<uint32_t>                      _unsorted; ///< With rules, the families whose members need a sort.
  bool                                       _reorder = false;
  std::vector<uint32_t>                      _oldest;
  PrometheusRenderStats                      _stats;
  bool                                       _warned_invalid = false;
  uint64_t                                   _renders        = 0;
  uint32_t                                   _position       = 0;
  std::string                                _body;
};
