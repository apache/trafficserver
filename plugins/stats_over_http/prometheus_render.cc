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

#include <charconv>
#include <cstdio>

#include "tsutil/DbgCtl.h"

namespace
{
DbgCtl dbg_ctl{"stats_over_http"};

std::string_view
type_name(PrometheusType type)
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

bool
is_numeric(TSRecordDataType data_type)
{
  return data_type == TS_RECORDDATATYPE_COUNTER || data_type == TS_RECORDDATATYPE_INT || data_type == TS_RECORDDATATYPE_FLOAT;
}
} // namespace

void
PrometheusRenderer::begin()
{
  ++_renders;
  _position = 0;
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
}

uint32_t
PrometheusRenderer::find(const char *name, TSRecordDataType data_type)
{
  if (auto it = _by_pointer.find(name); it != _by_pointer.end() && _records[it->second].data_type == data_type) {
    return it->second;
  }

  std::string key{name};
  uint32_t    index;

  if (auto it = _by_name.find(key); it != _by_name.end() && _records[it->second].data_type == data_type) {
    index = it->second;
  } else {
    index                    = translate(key, data_type);
    _by_name[std::move(key)] = index;
  }
  _by_pointer[name] = index;
  return index;
}

uint32_t
PrometheusRenderer::translate(std::string_view name, TSRecordDataType data_type)
{
  Record record;

  record.data_type = data_type;
  if (!is_numeric(data_type)) {
    Dbg(dbg_ctl, "Prometheus supports only numeric values, skipping: %.*s", static_cast<int>(name.size()), name.data());
  } else if (PrometheusName translated = _options.namer(name, data_type); !translated.family.empty()) {
    auto [it, added] = _family_index.try_emplace(translated.family, static_cast<uint32_t>(_families.size()));

    if (added) {
      Family &family = _families.emplace_back();

      family.type = translated.type;
      if (_options.help) {
        family.header.append("# HELP ").append(translated.family).append(" ").append(name).append("\n");
      }
      if (translated.type != PrometheusType::UNTYPED) {
        family.header.append("# TYPE ").append(translated.family).append(" ").append(type_name(translated.type)).append("\n");
      }
    } else if (_families[it->second].type != translated.type) {
      Dbg(dbg_ctl, "Inconsistent types for metric family %s: %.*s is %s, the family is %s", translated.family.c_str(),
          static_cast<int>(name.size()), name.data(), type_name(translated.type).data(),
          type_name(_families[it->second].type).data());
    }

    record.prefix = std::move(translated.family);
    if (!translated.labels.empty()) {
      record.prefix.append("{").append(translated.labels).append("}");
    }
    record.prefix += ' ';
    _families[it->second].members.push_back(static_cast<uint32_t>(_records.size()));
  }

  _records.push_back(std::move(record));
  return static_cast<uint32_t>(_records.size() - 1);
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

std::string &
PrometheusRenderer::render()
{
  _slots.resize(_position);
  _body.clear();

  for (const Family &family : _families) {
    bool header_written = false;

    for (uint32_t index : family.members) {
      const Record &record = _records[index];

      if (record.last_render != _renders) {
        continue;
      }
      if (!header_written) {
        _body          += family.header;
        header_written  = true;
      }
      _body += record.prefix;
      append_value(record);
    }
  }
  return _body;
}
