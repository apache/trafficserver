
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

#include "ts/ts.h"

#include "cripts/Lulu.hpp"
#include "cripts/Context.hpp"

namespace Cript
{
class IntConfig
{
public:
  IntConfig() = delete;
  explicit IntConfig(TSOverridableConfigKey key) : _key(key) {}

  // Implemented later in Configs.cc
  integer _get(Cript::Context *context) const;
  void    _set(Cript::Context *context, integer value);

private:
  const TSOverridableConfigKey _key;
}; // namespace Criptclass IntConfig

class FloatConfig
{
public:
  FloatConfig() = delete;
  explicit FloatConfig(TSOverridableConfigKey key) : _key(key) {}

  // Implemented later in Configs.cc
  float _get(Cript::Context *context) const;
  void  _set(Cript::Context *context, float value);

private:
  const TSOverridableConfigKey _key;
};

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
  bool      _set(const Cript::Context *context, const ValueType value);

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

private:
  Cript::string          _name;
  TSOverridableConfigKey _key  = TS_CONFIG_NULL;
  TSRecordDataType       _type = TS_RECORDDATATYPE_NULL;
};
} // namespace Cript
