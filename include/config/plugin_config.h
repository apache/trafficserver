/** @file

  Plugin configuration parsing and marshalling.

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

#include <string>
#include <vector>

#include "config/config_result.h"

namespace config
{

struct PluginConfigEntry {
  std::string              path;
  std::vector<std::string> args;
  bool                     enabled{true};
};

using PluginConfigData = std::vector<PluginConfigEntry>;

class PluginConfigParser
{
public:
  ConfigResult<PluginConfigData> parse(std::string const &filename);

private:
  ConfigResult<PluginConfigData> parse_legacy(std::string_view content);
};

class PluginConfigMarshaller
{
public:
  std::string to_yaml(PluginConfigData const &config);
};

} // namespace config
