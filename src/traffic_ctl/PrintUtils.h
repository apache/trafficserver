/**
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

#include <vector>
#include <utility>
#include <yaml-cpp/yaml.h>
#include <swoc/TextView.h>

// Record access control, indexed by RecAccessT.
[[maybe_unused]] static const char *
rec_accessof(int rec_access)
{
  switch (rec_access) {
  case 1:
    return "no access";
  case 2:
    return "read only";
  case 0: /* fallthrough */
  default:
    return "default";
  }
}

[[maybe_unused]] static const char *
rec_updateof(int rec_updatetype)
{
  switch (rec_updatetype) {
  case 1:
    return "dynamic, no restart";
  case 2:
    return "static, restart traffic_server";
  case 3:
    return "Oops, we shouldn't be using this update type";
  case 0: /* fallthrough */
  default:
    return "none";
  }
}

[[maybe_unused]] static const char *
rec_checkof(int rec_checktype)
{
  switch (rec_checktype) {
  case 1:
    return "string matching a regular expression";
  case 2:
    return "integer with a specified range";
  case 3:
    return "IP address";
  case 0: /* fallthrough */
  default:
    return "none";
  }
}

[[maybe_unused]] static const char *
rec_labelof(int rec_class)
{
  switch (rec_class) {
  case 1:
    return "CONFIG";
  case 16:
    return "LOCAL";
  default:
    return "unknown";
  }
}

[[maybe_unused]] static const char *
rec_sourceof(int rec_source)
{
  switch (rec_source) {
  case 1:
    return "built in default";
  case 3:
    return "administratively set";
  case 2:
    return "plugin default";
  case 4:
    return "environment";
  default:
    return "unknown";
  }
}

[[maybe_unused]] static bool WithDefaults{true};
[[maybe_unused]] static bool WithoutDefaults{false};

class RecNameToYaml
{
  using RecordInfo          = std::tuple<std::string /*name*/, std::string /*value*/, std::string /*def_val*/>;
  using RecList             = std::vector<std::pair<RecordInfo, bool /*keep track if a record was already converted.*/>>;
  using RecordsMatchTracker = std::vector<std::pair<RecordInfo &, bool &>>;

  RecordsMatchTracker find_all_keys_with_prefix(swoc::TextView prefix, RecList &vars);
  void                process_var_from_prefix(std::string prefix, RecList &vars, bool &in_a_map);
  void                build_yaml(RecList vars);

public:
  using RecInfoList = std::vector<RecordInfo>;
  RecNameToYaml()   = default;
  RecNameToYaml(RecInfoList recs, bool include_defaults);

  std::string
  string() const
  {
    return doc.good() ? doc.c_str() : "";
  }

private:
  bool          include_defaults{false};
  YAML::Emitter doc;
};
