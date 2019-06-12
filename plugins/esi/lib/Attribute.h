/** @file

  A brief file description

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

#pragma once

#include <list>

namespace EsiLib
{
struct Attribute {
  const char *name;
  int32_t name_len;
  const char *value;
  int32_t value_len;
  Attribute(const char *n = nullptr, int32_t n_len = 0, const char *v = nullptr, int32_t v_len = 0)
    : name(n), name_len(n_len), value(v), value_len(v_len){};
};

typedef std::list<Attribute> AttributeList;
}; // namespace EsiLib
