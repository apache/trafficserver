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

#include "prometheus_render.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numeric>

#include "prometheus_rules.h"
#include "tsutil/DbgCtl.h"
#include "tsutil/StringCompare.h"

namespace
{
DbgCtl dbg_ctl{"stats_over_http"};

bool
is_numeric(TSRecordDataType data_type)
{
  return data_type == TS_RECORDDATATYPE_COUNTER || data_type == TS_RECORDDATATYPE_INT || data_type == TS_RECORDDATATYPE_FLOAT;
}

bool
is_digit(char c)
{
  return c >= '0' && c <= '9';
}

// Accepts a decimal number with an optional sign and exponent, Inf or Infinity with an optional sign, or NaN.  The case of
// the letters does not matter.  Rejects a number too large for a double.
bool
parse_number(const std::string &text, double &value)
{
  std::string_view body{text};

  if (ts::iequals(body, "nan")) {
    value = NAN;
    return true;
  }
  if (!body.empty() && (body[0] == '+' || body[0] == '-')) {
    body.remove_prefix(1);
  }
  if (!ts::iequals(body, "inf") && !ts::iequals(body, "infinity")) {
    size_t i      = 0;
    size_t digits = 0;

    for (; i < body.size() && is_digit(body[i]); ++i) {
      ++digits;
    }
    if (i < body.size() && body[i] == '.') {
      for (++i; i < body.size() && is_digit(body[i]); ++i) {
        ++digits;
      }
    }
    if (digits == 0) {
      return false;
    }
    if (i < body.size() && (body[i] == 'e' || body[i] == 'E')) {
      size_t exponent = 0;

      if (++i < body.size() && (body[i] == '+' || body[i] == '-')) {
        ++i;
      }
      for (; i < body.size() && is_digit(body[i]); ++i) {
        ++exponent;
      }
      if (exponent == 0) {
        return false;
      }
    }
    if (i != body.size()) {
      return false;
    }
  }
  errno = 0;
  value = std::strtod(text.c_str(), nullptr);
  return !(errno == ERANGE && std::isinf(value));
}

// Appends @a value with the fewest significant digits, from @a min to @a max, that parse back to the same value.  With
// @a single, the digits must parse back to the same float.  NaN and the infinities get their Prometheus spellings.
void
append_number(std::string &out, double value, int min, int max, bool single)
{
  if (std::isnan(value)) {
    out += "NaN";
    return;
  }
  if (std::isinf(value)) {
    out += value > 0 ? "+Inf" : "-Inf";
    return;
  }

  char buffer[32];
  int  length = 0;

  for (int precision = min; precision <= max; ++precision) {
    length = snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if (single ? std::strtof(buffer, nullptr) == static_cast<float>(value) : std::strtod(buffer, nullptr) == value) {
      break;
    }
  }
  out.append(buffer, length);
}

std::string
join(const std::vector<std::string> &names)
{
  std::string result;

  for (const std::string &name : names) {
    if (!result.empty()) {
      result += ", ";
    }
    result += name;
  }
  return result;
}
} // namespace

std::string_view
prometheus_type_name(PrometheusType type)
{
  switch (type) {
  case PrometheusType::COUNTER:
    return "counter";
  case PrometheusType::GAUGE:
    return "gauge";
  case PrometheusType::UNTYPED:
    break;
  }
  return "untyped";
}

void
prometheus_escape_label_value(std::string &out, std::string_view value)
{
  for (char c : value) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out += c;
      break;
    }
  }
}

void
prometheus_escape_help_text(std::string &out, std::string_view text)
{
  for (char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out += c;
      break;
    }
  }
}

void
PrometheusRenderer::begin()
{
  ++_renders;
  _position = 0;
  _stats    = {};
}

void
PrometheusRenderer::add(const char *name, TSRecordDataType data_type, const TSRecordData &datum)
{
  uint32_t const position = _position++;
  uint32_t       index;

  if (position < _slots.size() && _slots[position].name == name && _records[_slots[position].record].data_type == data_type) {
    index = _slots[position].record;
  } else {
    index = find(name, data_type);
    if (position < _slots.size()) {
      _slots[position] = {name, index};
    } else {
      _slots.push_back({name, index});
    }
  }

  Record &record     = _records[index];
  record.last_render = _renders;
  record.datum       = datum;
  if (record.value == Value::TEXT_LABEL || record.value == Value::TEXT_NUMBER) {
    // The string is valid only while TSRecordDump calls back.
    _info[index].text.assign(datum.rec_string != nullptr ? datum.rec_string : "");
  } else if (record.value == Value::NONE && _info[index].invalid) {
    ++_stats.dropped;
  }
}

uint32_t
PrometheusRenderer::find(const char *name, TSRecordDataType data_type)
{
  if (auto it = _by_pointer.find(name); it != _by_pointer.end() && _records[it->second].data_type == data_type) {
    return it->second;
  }

  auto [it, added] = _by_name.try_emplace(name, 0);

  if (added || _records[it->second].data_type != data_type) {
    it->second = translate(it->first, data_type);
  }
  _by_pointer[name] = it->second;
  return it->second;
}

uint32_t
PrometheusRenderer::add_record(std::string_view name, TSRecordDataType data_type)
{
  auto const index = static_cast<uint32_t>(_records.size());

  _records.emplace_back().data_type = data_type;
  _info.emplace_back().name         = name;
  _writers.push_back({index, 0});
  return index;
}

uint32_t
PrometheusRenderer::add_family(const std::string &name, std::string_view record_name, PrometheusType type, uint32_t record)
{
  auto [it, added] = _family_index.try_emplace(name, static_cast<uint32_t>(_families.size()));

  if (added) {
    Family &family = _families.emplace_back();

    _family_info.push_back({name, record});
    _reorder    = true;
    family.type = type;
    if (_options.help) {
      family.header.append("# HELP ").append(name).append(" ");
      prometheus_escape_help_text(family.header, record_name);
      family.header.append("\n");
    }
    if (type != PrometheusType::UNTYPED) {
      family.header.append("# TYPE ").append(name).append(" ").append(prometheus_type_name(type)).append("\n");
    }
  }
  return it->second;
}

uint32_t
PrometheusRenderer::translate(std::string_view name, TSRecordDataType data_type)
{
  if (_options.rules != nullptr) {
    return translate_rules(name, data_type);
  }

  uint32_t const index = add_record(name, data_type);

  if (!is_numeric(data_type)) {
    Dbg(dbg_ctl, "Prometheus supports only numeric values, skipping: %.*s", static_cast<int>(name.size()), name.data());
  } else if (PrometheusName translated = _options.namer(name, data_type); !translated.family.empty()) {
    Record &record = _records[index];

    record.value        = Value::NUMBER;
    record.type         = translated.type;
    _info[index].family = add_family(translated.family, name, translated.type, index);

    Family &family = _families[_info[index].family];

    if (family.type != translated.type) {
      Dbg(dbg_ctl, "Inconsistent types for metric family %s: %.*s is %s, the family is %s", translated.family.c_str(),
          static_cast<int>(name.size()), name.data(), prometheus_type_name(translated.type).data(),
          prometheus_type_name(family.type).data());
    }
    record.prefix = std::move(translated.family);
    if (!translated.labels.empty()) {
      record.prefix.append("{").append(translated.labels).append("}");
    }
    record.prefix += ' ';
    family.members.push_back(index);
  }
  return index;
}

uint32_t
PrometheusRenderer::translate_rules(std::string_view name, TSRecordDataType data_type)
{
  uint32_t const            index  = add_record(name, data_type);
  PrometheusRuleMatch const match  = _options.rules->translate(name, data_type);
  Record                   &record = _records[index];
  RecordInfo               &info   = _info[index];

  if (match.result == PrometheusRuleMatch::Result::INVALID_NAME) {
    info.invalid = true;
    if (!_warned_invalid && _options.warn != nullptr) {
      _warned_invalid = true;
      _options.warn("Leaving out " + std::string{name} + ", because the rules name it " + match.family +
                    ", which is not a valid metric name.  Other such records are left out without a message");
    }
    return index;
  }
  if (match.result == PrometheusRuleMatch::Result::EXCLUDED) {
    return index;
  }

  if (match.text_label) {
    record.value = Value::TEXT_LABEL;
  } else {
    record.value = data_type == TS_RECORDDATATYPE_STRING ? Value::TEXT_NUMBER : Value::NUMBER;
  }
  record.type      = match.type;
  info.rule_index  = match.rule_index;
  info.label_names = match.label_names;
  info.family      = add_family(match.family, name, match.type, index);

  Family     &family      = _families[info.family];
  FamilyInfo &family_info = _family_info[info.family];

  family.members.push_back(index);
  mark_unsorted(info.family);
  if (family_info.definer != index && defines_over(index, family_info.definer)) {
    family_info.definer = index;
    regroup(info.family);
  } else {
    place(index, match.label_values);
  }
  return index;
}

void
PrometheusRenderer::place(uint32_t index, const std::vector<std::string> &label_values)
{
  Record                         &record  = _records[index];
  RecordInfo                     &info    = _info[index];
  FamilyInfo const               &family  = _family_info[info.family];
  const std::vector<std::string> &own     = *info.label_names;
  const std::vector<std::string> &defined = *_info[family.definer].label_names;
  bool const                      same    = std::is_permutation(own.begin(), own.end(), defined.begin(), defined.end());
  // The same label names in another order keep their values.  Other names take the names of the family by position.
  const std::vector<std::string> &names = same ? own : defined;

  _writers[index].first = index;
  record.dropped        = own.size() != defined.size() || other_kind(index);
  record.relabeled      = !record.dropped && !same;
  record.prefix.clear();
  if (record.dropped) {
    return;
  }

  struct Label {
    std::string_view name;
    std::string_view value;
    bool             text;
  };

  std::vector<Label> labels;

  labels.reserve(names.size() + _options.rules->const_labels().size());
  // The string value takes the last position.
  size_t const text = record.value == Value::TEXT_LABEL ? names.size() - 1 : names.size();

  for (size_t i = 0; i < names.size(); ++i) {
    labels.push_back({names[i], i == text ? std::string_view{} : std::string_view{label_values[i]}, i == text});
  }
  for (auto const &[name, value] : _options.rules->const_labels()) {
    labels.push_back({name, value, false});
  }
  std::sort(labels.begin(), labels.end(), [](const Label &a, const Label &b) { return a.name < b.name; });

  record.prefix = family.name;
  if (!labels.empty()) {
    record.prefix += '{';
    for (size_t i = 0; i < labels.size(); ++i) {
      if (i > 0) {
        record.prefix += ',';
      }
      record.prefix.append(labels[i].name).append("=\"");
      if (labels[i].text) {
        info.split = static_cast<uint32_t>(record.prefix.size());
      } else {
        prometheus_escape_label_value(record.prefix, labels[i].value);
      }
      record.prefix += '"';
    }
    record.prefix += '}';
  }
  record.prefix += ' ';

  size_t const hash        = std::hash<std::string>{}(record.prefix);
  auto const [first, last] = _series.equal_range(hash);

  for (auto it = first; it != last; ++it) {
    if (_records[it->second].prefix == record.prefix) {
      _writers[index].first = it->second;
      return;
    }
  }
  _series.emplace(hash, index);
}

void
PrometheusRenderer::forget_series(uint32_t index)
{
  Record const &record = _records[index];

  if (record.dropped || _writers[index].first != index) {
    return;
  }

  auto const [first, last] = _series.equal_range(std::hash<std::string>{}(record.prefix));

  for (auto it = first; it != last; ++it) {
    if (it->second == index) {
      _series.erase(it);
      return;
    }
  }
}

// The label names of the family changed, so place each member again.
void
PrometheusRenderer::regroup(uint32_t family)
{
  auto &members = _families[family].members;

  for (uint32_t member : members) {
    forget_series(member);
  }
  std::sort(members.begin(), members.end());
  for (uint32_t member : members) {
    place(member, _options.rules->translate(_info[member].name, _records[member].data_type).label_values);
  }
  mark_unsorted(family);
}

void
PrometheusRenderer::mark_unsorted(uint32_t family)
{
  if (!_family_info[family].unsorted) {
    _family_info[family].unsorted = true;
    _unsorted.push_back(family);
  }
}

// Sorts the families by name, and the members of each changed family by their labels.  Records with the same labels keep
// the order in which the renderer first saw them, which decides the one that writes their series.
void
PrometheusRenderer::sort_output()
{
  if (_reorder) {
    _reorder = false;
    _order.resize(_families.size());
    std::iota(_order.begin(), _order.end(), 0);
    std::sort(_order.begin(), _order.end(), [this](uint32_t a, uint32_t b) { return _family_info[a].name < _family_info[b].name; });
  }
  for (uint32_t family : _unsorted) {
    auto &members = _families[family].members;

    std::sort(members.begin(), members.end(), [this](uint32_t a, uint32_t b) {
      int const order = _records[a].prefix.compare(_records[b].prefix);

      return order < 0 || (order == 0 && a < b);
    });
    _family_info[family].unsorted = false;
  }
  _unsorted.clear();
}

// Whether record @a a sets the label names of a family rather than record @a b.  The later rule wins.  At the same rule,
// a record that is not TEXT_LABEL wins, so that the kind of the family does not depend on the order of the records.
bool
PrometheusRenderer::defines_over(uint32_t a, uint32_t b) const
{
  if (_info[a].rule_index != _info[b].rule_index) {
    return _info[a].rule_index > _info[b].rule_index;
  }
  return _records[a].value != Value::TEXT_LABEL && _records[b].value == Value::TEXT_LABEL;
}

// Whether exactly one of the record and the record that sets the label names of its family is a TEXT_LABEL record.
bool
PrometheusRenderer::other_kind(uint32_t index) const
{
  uint32_t const definer = _family_info[_info[index].family].definer;

  return (_records[index].value == Value::TEXT_LABEL) != (_records[definer].value == Value::TEXT_LABEL);
}

// Logs the first record of each family that has a change of this kind.
void
PrometheusRenderer::report(uint32_t index, Warning warning)
{
  RecordInfo const &info   = _info[index];
  FamilyInfo       &family = _family_info[info.family];

  if (_options.warn == nullptr || (family.warned & warning) != 0) {
    return;
  }
  family.warned |= warning;

  std::string const               name{info.name};
  std::string const               definer{_info[family.definer].name};
  const std::vector<std::string> &names = *_info[family.definer].label_names;

  switch (warning) {
  case WARN_LABELS:
    _options.warn("Leaving out " + name + ", because its labels (" + join(*info.label_names) +
                  ") differ in number from the labels (" + join(names) + ") of " + family.name);
    break;
  case WARN_RELABELED:
    _options.warn("Writing " + name + " with the labels (" + join(names) + ") of " + family.name + " instead of its own labels (" +
                  join(*info.label_names) + ")");
    break;
  case WARN_DUPLICATE:
    _options.warn(name +
                  (_records[index].value == Value::TEXT_LABEL ? " has the same family and labels, apart from its string, as " :
                                                                " has the same series as ") +
                  std::string{_info[_writers[index].first].name} + ", so each render writes only the first of them that it finds");
    break;
  case WARN_KIND:
    if (_records[index].value == Value::TEXT_LABEL) {
      _options.warn("Leaving out " + name + ", because it is a string metric in prometheus.strings.names and " + family.name +
                    " takes its label names from " + definer + ", which is not");
    } else {
      _options.warn("Leaving out " + name + ", because " + family.name + " takes its label names from " + definer +
                    ", a string metric in prometheus.strings.names");
    }
    break;
  case WARN_TYPE:
    _options.warn("Writing " + name + " with the type (" + std::string{prometheus_type_name(_families[info.family].type)} +
                  ") of " + family.name + " instead of its own type (" + std::string{prometheus_type_name(_records[index].type)} +
                  ")");
    break;
  }
}

void
PrometheusRenderer::append_value(const Record &record)
{
  char  buffer[32];
  char *end = buffer;

  switch (record.data_type) {
  case TS_RECORDDATATYPE_COUNTER: {
    auto value = static_cast<uint64_t>(record.datum.rec_counter);

    if (_options.wrap_counters && value > INT64_MAX) {
      value %= INT64_MAX;
    }
    end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
    break;
  }
  case TS_RECORDDATATYPE_INT:
    end = std::to_chars(buffer, buffer + sizeof(buffer), record.datum.rec_int).ptr;
    break;
  case TS_RECORDDATATYPE_FLOAT:
    end += snprintf(buffer, sizeof(buffer), "%g", record.datum.rec_float);
    break;
  default:
    break;
  }
  *end++ = '\n';
  _body.append(buffer, end);
}

// Whether the render writes the sample of a record with rules.  Counts the samples that it leaves out.
bool
PrometheusRenderer::take(uint32_t index)
{
  Record &record = _records[index];

  if (record.last_render != _renders) {
    return false;
  }
  if (record.dropped) {
    ++_stats.dropped;
    report(index, other_kind(index) ? WARN_KIND : WARN_LABELS);
    return false;
  }
  if (record.value == Value::TEXT_NUMBER && !parse_number(_info[index].text, _info[index].number)) {
    return false;
  }

  Writer &first = _writers[_writers[index].first];

  if (first.written == _renders) {
    ++_stats.duplicates;
    report(index, WARN_DUPLICATE);
    return false;
  }
  first.written = _renders;
  return true;
}

// Writes the sample of a record with rules.
void
PrometheusRenderer::write(const Family &family, uint32_t index, bool &header)
{
  Record const     &record = _records[index];
  RecordInfo const &info   = _info[index];

  if (!header) {
    _body  += family.header;
    header  = true;
  }
  if (record.relabeled) {
    report(index, WARN_RELABELED);
  }
  if (record.type != family.type) {
    report(index, WARN_TYPE);
  }
  switch (record.value) {
  case Value::TEXT_LABEL:
    _body.append(record.prefix, 0, info.split);
    prometheus_escape_label_value(_body, info.text);
    _body.append(record.prefix, info.split).append("1\n");
    return;
  case Value::TEXT_NUMBER:
    _body += record.prefix;
    append_number(_body, info.number, std::numeric_limits<double>::digits10, std::numeric_limits<double>::max_digits10, false);
    _body += '\n';
    return;
  default:
    break;
  }
  _body += record.prefix;
  if (record.data_type == TS_RECORDDATATYPE_FLOAT) {
    append_number(_body, record.datum.rec_float, std::numeric_limits<float>::digits10, std::numeric_limits<float>::max_digits10,
                  true);
    _body += '\n';
  } else {
    append_value(record);
  }
}

std::string &
PrometheusRenderer::render()
{
  // Local counts stay in registers across the appends to the body.
  uint64_t series         = 0;
  uint64_t relabeled      = 0;
  uint64_t type_conflicts = 0;
  auto     wrote          = [&](const Family &family, uint32_t index) {
    ++series;
    relabeled      += _records[index].relabeled;
    type_conflicts += _records[index].type != family.type;
  };

  _slots.resize(_position);
  _body.clear();

  uint64_t const limit = _options.rules != nullptr ? _options.rules->max_series() : 0;

  if (_options.rules == nullptr) {
    // Each record writes its own series.
    for (const Family &family : _families) {
      bool header = false;

      for (uint32_t index : family.members) {
        const Record &record = _records[index];

        if (record.last_render != _renders) {
          continue;
        }
        if (!header) {
          _body  += family.header;
          header  = true;
        }
        _body += record.prefix;
        append_value(record);
        wrote(family, index);
      }
    }
  } else if (limit == 0) {
    sort_output();
    for (uint32_t f : _order) {
      const Family &family = _families[f];
      bool          header = false;

      for (uint32_t index : family.members) {
        if (take(index)) {
          write(family, index, header);
          wrote(family, index);
        }
      }
    }
  } else {
    sort_output();
    _selected.clear();
    for (uint32_t f : _order) {
      for (uint32_t index : _families[f].members) {
        if (take(index)) {
          _selected.push_back(index);
        }
      }
    }
    // The oldest records keep their series, so that each render writes the same series.
    if (_selected.size() > limit) {
      _oldest = _selected;
      std::nth_element(_oldest.begin(), _oldest.begin() + (limit - 1), _oldest.end());

      uint32_t const newest = _oldest[limit - 1];

      _stats.dropped += _selected.size() - limit;
      std::erase_if(_selected, [newest](uint32_t index) { return index > newest; });
    }

    uint32_t current = UINT32_MAX;
    bool     header  = false;

    for (uint32_t index : _selected) {
      if (_info[index].family != current) {
        current = _info[index].family;
        header  = false;
      }
      write(_families[current], index, header);
      wrote(_families[current], index);
    }
  }
  _stats.series         += series;
  _stats.relabeled      += relabeled;
  _stats.type_conflicts += type_conflicts;
  return _body;
}
