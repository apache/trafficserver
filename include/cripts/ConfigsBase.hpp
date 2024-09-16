
/*
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

#include <unordered_map>

#include "ts/ts.h"
#include "cripts/Lulu.hpp"
#include "cripts/Context.hpp"

namespace cripts
{

class Records
{
  using self_type = Records;

public:
  using ValueType = std::variant<TSMgmtInt, TSMgmtFloat, std::string>;

  Records(const self_type &)              = delete;
  Records()                               = delete;
  self_type &operator=(const self_type &) = delete;

  Records(self_type &&that) = default;

  explicit Records(const cripts::string_view name);

  ValueType _get(const cripts::Context *context) const;
  bool      _set(const cripts::Context *context, const ValueType &value) const;

  // Optimizations, we can't use string_view in the ValueType since we need persistent storage.
  // Be careful with the setSV() and make sure there's underlying storage!
  const cripts::string_view GetSV(const cripts::Context *context) const;
  bool                      SetSV(const cripts::Context *context, const cripts::string_view value) const;

  [[nodiscard]] TSOverridableConfigKey
  Key() const
  {
    return _key;
  }

  [[nodiscard]] TSRecordDataType
  Type() const
  {
    return _type;
  }

  [[nodiscard]] cripts::string_view
  Name() const
  {
    return _name;
  }

  [[nodiscard]] bool
  Loaded() const
  {
    return (_key != TS_CONFIG_NULL && _type != TS_RECORDDATATYPE_NULL);
  }

  [[nodiscard]] bool
  IsInteger() const
  {
    return _type == TS_RECORDDATATYPE_INT;
  }

  [[nodiscard]] bool
  IsFloat() const
  {
    return _type == TS_RECORDDATATYPE_FLOAT;
  }

  [[nodiscard]] bool
  IsString() const
  {
    return _type == TS_RECORDDATATYPE_STRING;
  }

  static void           Add(const Records *rec);
  static const Records *Lookup(const cripts::string_view name);

private:
  cripts::string         _name;
  TSOverridableConfigKey _key  = TS_CONFIG_NULL;
  TSRecordDataType       _type = TS_RECORDDATATYPE_NULL;

  static std::unordered_map<cripts::string_view, const Records *> _gRecords;
}; // class Records

class IntConfig
{
public:
  IntConfig() = delete;
  explicit IntConfig(const cripts::string_view name) : _record(name) { Records::Add(&_record); }

  float
  _get(cripts::Context *context) const
  {
    return std::get<TSMgmtInt>(_record._get(context));
  }

  void
  _set(cripts::Context *context, integer value)
  {
    _record._set(context, value);
  }

private:
  const Records _record;
}; // class IntConfig

class FloatConfig
{
public:
  FloatConfig() = delete;
  explicit FloatConfig(const cripts::string_view name) : _record(name) { Records::Add(&_record); }

  float
  _get(cripts::Context *context) const
  {
    return std::get<TSMgmtFloat>(_record._get(context));
  }

  void
  _set(cripts::Context *context, float value)
  {
    _record._set(context, value);
  }

private:
  const Records _record;
}; // class FloatConfig

class StringConfig
{
public:
  StringConfig() = delete;
  explicit StringConfig(const cripts::string_view name) : _record(name) { Records::Add(&_record); }

  std::string
  _get(cripts::Context *context) const
  {
    return std::get<std::string>(_record._get(context));
  }

  void
  _set(cripts::Context *context, std::string &value)
  {
    _record.SetSV(context, value);
  }

  // Only for the string type!
  const std::string_view
  GetSV(cripts::Context *context) const
  {
    return _record.GetSV(context);
  }

private:
  const Records _record;
}; // class StringConfig

} // namespace cripts
