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

void
Value::set_value(const std::string &val)
{
  _value = val;

  if (_value.find("%{") != std::string::npos || _value.find("%<") != std::string::npos || _value.find("\"") != std::string::npos) {
    Parser parser(_value);
    auto tokens = parser.get_tokens();
    for (auto it = tokens.begin(); it != tokens.end(); it++) {
      Parser tparser(*it);

      Condition *tcond_val = nullptr;
      if ((*it).substr(0, 2) == "%<") {
        tcond_val = new ConditionExpandableString(*it);
      } else if ((*it) == "+") {
        // Skip concat token
        continue;
      } else {
        tcond_val = condition_factory(tparser.get_op());

        if (tcond_val) {
          tcond_val->initialize(tparser);
        } else {
          tcond_val = new ConditionStringLiteral(*it);
        }
      }
      _cond_vals.push_back(tcond_val);
    }
  } else {
    _int_value   = strtol(_value.c_str(), nullptr, 10);
    _float_value = strtod(_value.c_str(), nullptr);
  }
}
