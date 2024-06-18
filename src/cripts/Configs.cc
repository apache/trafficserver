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
integer
IntConfig::_get(Cript::Context *context) const
{
  integer value = -1;

  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigIntGet(context->state.txnp, _key, &value) != TS_SUCCESS) {
    context->state.error.fail();
  }

  return value;
}

void
IntConfig::_set(Cript::Context *context, integer value)
{
  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigIntSet(context->state.txnp, _key, static_cast<TSMgmtInt>(value)) != TS_SUCCESS) {
    context->state.error.fail();
  }
}

float
FloatConfig::_get(Cript::Context *context) const
{
  float value = -1;

  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigFloatGet(context->state.txnp, _key, &value) != TS_SUCCESS) {
    context->state.error.fail();
  }

  return value;
}

void
FloatConfig::_set(Cript::Context *context, float value)
{
  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigFloatSet(context->state.txnp, _key, static_cast<TSMgmtFloat>(value)) != TS_SUCCESS) {
    context->state.error.fail();
  }
}

Records::Records(const Cript::string_view name)
{
  TSOverridableConfigKey key;
  TSRecordDataType       type;

  if (TSHttpTxnConfigFind(name.data(), name.size(), &key, &type) != TS_SUCCESS) {
    TSError("Invalid configuration variable '%.*s'", static_cast<int>(name.size()), name.data());
    TSReleaseAssert(!"Invalid configuration variable");
  } else {
    _name = name;
    _key  = key;
    _type = type;
  }
}

Records::ValueType
Records::_get(const Cript::Context *context) const
{
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
    const char *s   = nullptr;
    int         len = 0;

    if (TSHttpTxnConfigStringGet(context->state.txnp, _key, &s, &len) == TS_SUCCESS) {
      return std::string(s, len);
    }
  } break;
  default:
    TSReleaseAssert(!"Invalid configuration type");
    return 0;
  }

  return 0;
}

bool
Records::_set(const Cript::Context *context, const ValueType value)
{
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

    if (TSHttpTxnConfigStringSet(context->state.txnp, _key, str.c_str(), str.size()) != TS_SUCCESS) {
      TSError("Failed to set string configuration '%s'", _name.c_str());
      return false;
    }
    CDebug("Set string configuration '{}' to '{}'", _name.c_str(), str.c_str());
  } break;
  default:
    TSReleaseAssert(!"Invalid configuration type");
    return false;
  }

  return true; // Success
}

} // namespace Cript
