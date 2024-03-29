/** @file

  Map of transaction overridable configuration variables and names.

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

#include "ts/apidefs.h"

#include <string_view>
#include <unordered_map>

namespace ts
{
/// Map of configuration variable name to enum and type for all transaction overridable variables.
extern const std::unordered_map<std::string_view, std::tuple<const TSOverridableConfigKey, const TSRecordDataType>>
  Overridable_Txn_Vars;
} // namespace ts
