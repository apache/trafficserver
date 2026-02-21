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

#include <string>

#include "hrw4u/ObjTypes.h"

namespace hrw
{
struct ConditionSpec {
  ConditionType type = ConditionType::NONE;
  std::string   qualifier;
  std::string   match_arg;
  int           slot = -1;

  bool mod_not    = false;
  bool mod_or     = false;
  bool mod_and    = false;
  bool mod_nocase = false;
  bool mod_last   = false;
  bool mod_ext    = false;
  bool mod_pre    = false;
};

struct OperatorSpec {
  OperatorType type = OperatorType::NONE;
  std::string  arg;
  std::string  value;
  int          slot = -1;

  bool mod_last = false;
  bool mod_qsa  = false;
  bool mod_inv  = false;
};

} // namespace hrw
