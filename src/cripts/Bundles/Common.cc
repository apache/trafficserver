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

#include "cripts/Preamble.hpp"
#include "cripts/Bundles/Common.hpp"

namespace Bundle
{
const Cript::string Common::_name = "Bundle::Common";

bool
Common::validate(std::vector<Cript::Bundle::Error> &errors) const
{
  bool good = true;

  // The .dscp() can only be 0 - 63
  if (_dscp < 0 || _dscp > 63) {
    errors.emplace_back("dscp must be between 0 and 63", name(), "dscp");
    good = false;
  }

  // Make sure we didn't encounter an error setting up the via headers
  if (_client_via.first == 999 || _origin_via.first == 999) {
    errors.emplace_back("via_header must be one of: disable, protocol, basic, detailed, full", name(), "via_header");
    good = false;
  }

  // Make sure all configurations are of the correct type
  for (auto &[rec, value] : _configs) {
    switch (rec->type()) {
    case TS_RECORDDATATYPE_INT:
      if (!std::holds_alternative<TSMgmtInt>(value)) {
        errors.emplace_back("Invalid value for config, expecting an integer", name(), rec->name());
        good = false;
      }
      break;
    case TS_RECORDDATATYPE_FLOAT:
      if (!std::holds_alternative<TSMgmtFloat>(value)) {
        errors.emplace_back("Invalid value for config, expecting a float", name(), rec->name());
        good = false;
      }
      break;
    case TS_RECORDDATATYPE_STRING:
      if (!std::holds_alternative<std::string>(value)) {
        errors.emplace_back("Invalid value for config, expecting an integer", name(), rec->name());
        good = false;
      }
      break;

    default:
      errors.emplace_back("Invalid configuration type", name(), rec->name());
      good = false;
      break;
    }
  }

  return good;
}

Common::self_type &
Common::via_header(const Cript::string_view &destination, const Cript::string_view &value)
{
  static const std::unordered_map<Cript::string_view, int> SettingValues = {
    {"disable",  0  },
    {"protocol", 1  },
    {"basic",    2  },
    {"detailed", 3  },
    {"full",     4  },
    {"none",     999}  // This is an error
  };

  needCallback(Cript::Callbacks::DO_REMAP);

  int  val = 999;
  auto it  = SettingValues.find(value);

  if (it != SettingValues.end()) {
    val = it->second;
  }

  if (destination.starts_with("client")) {
    _client_via = {val, true};
  } else if (destination.starts_with("origin") || destination.starts_with("server")) {
    _origin_via = {val, true};
  } else {
    _client_via = _origin_via = {999, false};
  }

  return *this;
}

Common &
Common::set_config(const Cript::string_view name, const Cript::Records::ValueType &value)
{
  auto rec = Cript::Records::lookup(name); // These should be loaded at startup

  if (rec) {
    needCallback(Cript::Callbacks::DO_REMAP);
    _configs.emplace_back(rec, value);
  } else {
    CFatal("[Common::set_config]: Unknown configuration '%.*s'", static_cast<int>(name.size()), name.data());
  }

  return *this;
}

Common &
Common::set_config(const std::vector<std::pair<const Cript::string_view, const Cript::Records::ValueType>> &configs)
{
  for (auto &[name, value] : configs) {
    set_config(name, value);
  }

  return *this;
}

void
Common::doRemap(Cript::Context *context)
{
  // .dscp(int)
  if (_dscp > 0) {
    borrow conn = Client::Connection::get();

    CDebug("Setting DSCP = {}", _dscp);
    conn.dscp = _dscp;
  }

  // .via_header()
  if (_client_via.second) {
    CDebug("Setting Client Via = {}", _client_via.first);
    proxy.config.http.insert_response_via_str.set(_client_via.first);
  }
  if (_origin_via.second) {
    CDebug("Setting Origin Via = {}", _origin_via.first);
    proxy.config.http.insert_request_via_str.set(_origin_via.first);
  }

  // .set_config()
  for (auto &[rec, value] : _configs) {
    rec->_set(context, value);
  }
}

} // namespace Bundle
