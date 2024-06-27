
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

namespace Cript
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

  explicit Records(const Cript::string_view name);

  ValueType _get(const Cript::Context *context) const;
  bool      _set(const Cript::Context *context, const ValueType &value) const;

  // Optimizations, we can't use string_view in the ValueType since we need persistent storage.
  // Be careful with the setSV() and make sure there's underlying storage!
  const Cript::string_view getSV(const Cript::Context *context) const;
  bool                     setSV(const Cript::Context *context, const Cript::string_view value) const;

  [[nodiscard]] TSOverridableConfigKey
  key() const
  {
    return _key;
  }

  [[nodiscard]] TSRecordDataType
  type() const
  {
    return _type;
  }

  [[nodiscard]] Cript::string_view
  name() const
  {
    return _name;
  }

  [[nodiscard]] bool
  loaded() const
  {
    return (_key != TS_CONFIG_NULL && _type != TS_RECORDDATATYPE_NULL);
  }

  [[nodiscard]] bool
  isInteger() const
  {
    return _type == TS_RECORDDATATYPE_INT;
  }

  [[nodiscard]] bool
  isFloat() const
  {
    return _type == TS_RECORDDATATYPE_FLOAT;
  }

  [[nodiscard]] bool
  isString() const
  {
    return _type == TS_RECORDDATATYPE_STRING;
  }

  static void           add(const Records *rec);
  static const Records *lookup(const Cript::string_view name);

private:
  Cript::string          _name;
  TSOverridableConfigKey _key  = TS_CONFIG_NULL;
  TSRecordDataType       _type = TS_RECORDDATATYPE_NULL;

  static std::unordered_map<Cript::string_view, const Records *> _gRecords;
}; // class Records

class IntConfig
{
public:
  IntConfig() = delete;
  explicit IntConfig(const Cript::string_view name) : _record(name) { Records::add(&_record); }

  float
  _get(Cript::Context *context) const
  {
    return std::get<TSMgmtInt>(_record._get(context));
  }

  void
  _set(Cript::Context *context, integer value)
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
  explicit FloatConfig(const Cript::string_view name) : _record(name) { Records::add(&_record); }

  float
  _get(Cript::Context *context) const
  {
    return std::get<TSMgmtFloat>(_record._get(context));
  }

  void
  _set(Cript::Context *context, float value)
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
  explicit StringConfig(const Cript::string_view name) : _record(name) { Records::add(&_record); }

  std::string
  _get(Cript::Context *context) const
  {
    return std::get<std::string>(_record._get(context));
  }

  void
  _set(Cript::Context *context, std::string &value)
  {
    _record.setSV(context, value);
  }

  const std::string_view
  getSV(Cript::Context *context) const
  {
    return _record.getSV(context);
  }

private:
  const Records _record;
}; // class StringConfig

} // namespace Cript
