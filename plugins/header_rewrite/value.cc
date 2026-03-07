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

#include <string>

#include "value.h"

#include "condition.h"
#include "objtypes.h"
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

  if (_value.find("%{") != std::string::npos) {
    HRWSimpleTokenizer tokenizer(_value);
    auto               tokens = tokenizer.get_tokens();

    for (const auto &token : tokens) {
      Condition *tcond_val = nullptr;

      if (token.substr(0, 2) == "%{") {
        // The cond_token format is "COND:qualifier" or "COND:qualifier arg"
        std::string cond_token = token.substr(2, token.size() - 3);
        std::string cond_name;
        std::string cond_arg;
        auto        space_pos = cond_token.find(' ');

        if (space_pos != std::string::npos) {
          cond_name = cond_token.substr(0, space_pos);
          cond_arg  = cond_token.substr(space_pos + 1);
        } else {
          cond_name = cond_token;
        }

        auto spec = hrw::parse_condition_string(cond_name, cond_arg);

        tcond_val = hrw::create_condition(spec);
        if (tcond_val) {
          require_resources(tcond_val->get_resource_ids());
        } else {
          Dbg(dbg_ctl, "Error creating condition for value '%s'", _value.c_str());
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
