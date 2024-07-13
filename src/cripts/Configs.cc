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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

namespace Cript
{

std::unordered_map<Cript::string_view, const Records *> Records::_gRecords;

Records::Records(const Cript::string_view name)
{
  TSOverridableConfigKey key;
  TSRecordDataType       type;

  if (TSHttpTxnConfigFind(name.data(), name.size(), &key, &type) == TS_SUCCESS) {
    _name = name;
    _key  = key;
    _type = type;
  } else {
    CFatal("[Records]: Invalid configuration variable '%.*s'", static_cast<int>(name.size()), name.data());
  }
}

Records::ValueType
Records::_get(const Cript::Context *context) const
{
  TSAssert(context->state.txnp);

  switch (_type) {
  case TS_RECORDDATATYPE_INT: {
    TSMgmtInt i;

    if (TSHttpTxnConfigIntGet(context->state.txnp, _key, &i) == TS_SUCCESS) {
      return i;
    }
  } break;
  case TS_RECORDDATATYPE_FLOAT: {
    TSMgmtFloat f;

    if (TSHttpTxnConfigFloatGet(context->state.txnp, _key, &f) == TS_SUCCESS) {
      return f;
    }
  } break;
  case TS_RECORDDATATYPE_STRING: {
    return std::string{GetSV(context)};
  } break;
  default:
    CFatal("[Records]: Invalid configuration type");
    return 0;
  }

  return 0;
}

const Cript::string_view
Records::GetSV(const Cript::Context *context) const
{
  TSAssert(context->state.txnp);

  switch (_type) {
  case TS_RECORDDATATYPE_STRING: {
    const char *s   = nullptr;
    int         len = 0;

    if (TSHttpTxnConfigStringGet(context->state.txnp, _key, &s, &len) == TS_SUCCESS) {
      return {s, len};
    }
  } break;
  default:
    CFatal("[Records]: Invalid configuration type for GetSV()");
    break;
  }

  return {};
}

bool
Records::_set(const Cript::Context *context, const ValueType &value) const
{
  TSAssert(context->state.txnp);

  switch (_type) {
  case TS_RECORDDATATYPE_INT: {
    TSMgmtInt i = std::get<TSMgmtInt>(value);

    if (TSHttpTxnConfigIntSet(context->state.txnp, _key, i) != TS_SUCCESS) {
      TSError("Failed to set integer configuration '%s'", _name.c_str());
      return false;
    }
    CDebug("Set integer configuration '{}' to {}", _name.c_str(), i);
  } break;
  case TS_RECORDDATATYPE_FLOAT: {
    TSMgmtFloat f = std::get<TSMgmtFloat>(value);

    if (TSHttpTxnConfigFloatSet(context->state.txnp, _key, f) != TS_SUCCESS) {
      TSError("Failed to set float configuration '%s'", _name.c_str());
      return false;
    }
    CDebug("Set float configuration '{}' to {}", _name.c_str(), f);
  } break;
  case TS_RECORDDATATYPE_STRING: {
    auto &str = std::get<std::string>(value);

    SetSV(context, {str.data(), str.size()});
  } break;
  default:
    CFatal("[Records]: Invalid configuration type");
    return false;
  }

  return true; // Success
}

bool
Records::SetSV(const Cript::Context *context, const Cript::string_view value) const
{
  TSAssert(context->state.txnp);

  switch (_type) {
  case TS_RECORDDATATYPE_STRING: {
    if (TSHttpTxnConfigStringSet(context->state.txnp, _key, value.data(), value.size()) != TS_SUCCESS) {
      TSError("Failed to set string configuration '%s'", _name.c_str());
      return false;
    }
    CDebug("Set string configuration '{}' to '{}'", _name.c_str(), value);
  } break;
  default:
    CFatal("[Records]: Invalid configuration type for setSV()");
    return false;
  }

  return true; // Success
}

// Static members for the records "cache"
void
Records::Add(const Records *rec)
{
  CAssert(rec->Loaded());
  auto it = _gRecords.find(rec->Name());

  CAssert(it == _gRecords.end());
  _gRecords[rec->Name()] = rec;
}

const Records *
Records::Lookup(const Cript::string_view name)
{
  auto it = _gRecords.find(name);

  if (it != _gRecords.end()) {
    CAssert(it->second->Loaded());
    return it->second;
  }

  return nullptr;
}

} // namespace Cript
