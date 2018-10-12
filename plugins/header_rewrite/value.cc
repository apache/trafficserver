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
#include "factory.h"
#include "parser.h"
#include "conditions.h"

Value::~Value()
{
  TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for Value");
  for (auto it = _cond_vals.begin(); it != _cond_vals.end(); it++) {
    delete *it;
  }
}

void
Value::set_value(const std::string &val)
{
  _value = val;

  if (_value.find("%{") != std::string::npos) {
    SimpleTokenizer tokenizer(_value);
    auto tokens = tokenizer.get_tokens();

    for (auto it = tokens.begin(); it != tokens.end(); it++) {
      std::string token    = *it;
      Condition *tcond_val = nullptr;

      if (token.substr(0, 2) == "%{") {
        std::string cond_token = token.substr(2, token.size() - 3);

        if ((tcond_val = condition_factory(cond_token))) {
          Parser parser(_value);

          tcond_val->initialize(parser);
        }
      } else {
        tcond_val = new ConditionStringLiteral(token);
      }

      if (tcond_val) {
        _cond_vals.push_back(tcond_val);
      }
    }
  } else {
    _int_value   = strtol(_value.c_str(), nullptr, 10);
    _float_value = strtod(_value.c_str(), nullptr);
  }
}
