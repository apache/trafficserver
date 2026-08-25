/* @file

  Implementation for creating all values.

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

#include <cctype>
#include <string>

#include "swoc/TextView.h"

#include "value.h"

#include "condition.h"
#include "factory.h"
#include "parser.h"
#include "conditions.h"

Value::~Value()
{
  Dbg(dbg_ctl, "Calling DTOR for Value");
}

void
Value::set_value(const std::string &val, Statement *owner)
{
  _value = val;
  if (owner && owner->has_config_location()) {
    set_config_location(owner->get_config_filename().c_str(), owner->get_config_lineno());
  }

  if (_value.find("%{") != std::string::npos) {
    HRWSimpleTokenizer tokenizer(_value);
    auto               tokens = tokenizer.get_tokens();

    for (const auto &token : tokens) {
      Condition *tcond_val = nullptr;

      if (token.substr(0, 2) == "%{") {
        std::string    cond_token = token.substr(2, token.size() - 3);
        swoc::TextView cond_name{cond_token};

        // The factory only wants the condition and its qualifier, so hide any trailing
        // [MODS] from it. The Parser still sees them, and initialize() consumes them.
        if (cond_name.ends_with(']')) {
          if (auto pos = cond_name.rfind('['); pos != swoc::TextView::npos) {
            cond_name.remove_suffix(cond_name.size() - pos);
            cond_name.rtrim_if([](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
          }
        }

        if ((tcond_val = condition_factory(std::string{cond_name}))) {
          Parser parser;

          if (parser.parse_line(cond_token)) {
            if (has_config_location()) {
              tcond_val->set_config_location(get_config_filename().c_str(), get_config_lineno());
            }
            tcond_val->initialize(parser);
            require_resources(tcond_val->get_resource_ids());
          } else {
            // TODO: should we produce error here?
            Dbg(dbg_ctl, "Error parsing value '%s'", _value.c_str());
          }
        }
      } else {
        tcond_val = new ConditionStringLiteral(token);
      }

      if (tcond_val) {
        _cond_vals.push_back(std::unique_ptr<Condition>{tcond_val});
      }
    }

    // If we have an owner (e.g. an Operator) hoist up the resource requirements
    if (owner) {
      owner->require_resources(get_resource_ids());
    }
  } else {
    _int_value   = strtol(_value.c_str(), nullptr, 10);
    _float_value = strtod(_value.c_str(), nullptr);
  }
}
