/** @file

  YAML based cache configs

  TODO: Add YamlCacheConfig, YamlHostingConfig and YamlStorageConfig

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
#include <string_view>

struct SpanConfigParams {
  std::string id;
  std::string path;
  std::string hash_seed; ///< optional
  int64_t size = 0;      ///< optional for raw device
};

using SpanConfig = std::vector<SpanConfigParams>;
struct ConfigVolumes;

struct YamlStorageConfig {
  static bool load_volumes(ConfigVolumes &v_config, std::string &yaml);
  static bool load(SpanConfig &s_config, ConfigVolumes &v_config, std::string_view filename);
};
