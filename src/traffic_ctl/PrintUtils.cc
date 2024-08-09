/** @file

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

#include "PrintUtils.h"
#include <swoc/bwf_base.h>

namespace
{
void
YAML_begin_map(YAML::Emitter &doc)
{
  doc << YAML::BeginMap;
}
void
YAML_end_map(YAML::Emitter &doc)
{
  doc << YAML::EndMap;
}
void
YAML_add_key(YAML::Emitter &doc, std::string key)
{
  doc << YAML::Key << key;
}
void
YAML_add_value(YAML::Emitter &doc, std::string value)
{
  doc << YAML::Value << value;
}
void
YAML_add_comment(YAML::Emitter &doc, std::string comment, const char *fmt = "default: {}")
{
  std::string txt;
  doc << YAML::Comment(swoc::bwprint(txt, fmt, comment));
}
void
remove_legacy_config_prefix(swoc::TextView &rec_name)
{
  constexpr std::array<swoc::TextView, 3> Prefixes{"proxy.config.", "local.config.", "proxy.node."};

  for (auto &prefix : Prefixes) {
    if (rec_name.starts_with(prefix)) {
      rec_name.remove_prefix(prefix.size());
      break;
    }
  }
}
} // namespace

RecNameToYaml::RecNameToYaml(RecInfoList records, bool p_include_defaults) : include_defaults(p_include_defaults)
{
  if (records.size() == 0) {
    return;
  }

  RecList recs;

  std::sort(records.begin(), records.end(), [](const auto &lhs, const auto &rhs) {
    // by the name.
    return std::get<0>(lhs) < std::get<0>(rhs);
  });

  // remove duplicated
  records.erase(std::unique(records.begin(), records.end(),
                            [](const auto &lhs, const auto &rhs) { return std::get<0>(lhs) == std::get<0>(rhs); }),
                records.end());

  // work from this list of records.
  for (auto const &e : records) {
    recs.push_back(std::make_pair(e, false));
  }

  build_yaml(recs);
}

RecNameToYaml::RecordsMatchTracker
RecNameToYaml::find_all_keys_with_prefix(swoc::TextView prefix, RecNameToYaml::RecList &vars)
{
  RecordsMatchTracker ret;
  for (auto &p : vars) {
    if (p.second) {
      continue;
    }

    swoc::TextView name{std::get<0>(p.first)};
    remove_legacy_config_prefix(name);

    // A var name could also start with the same prefix and not being a field
    // like prefix=logfile and field=logfile_perm so we need to make sure we are either the end of the field
    // or the end of the level(with another .)
    if (!name.starts_with(prefix) || name.at(prefix.size()) != '.') {
      continue;
    }
    ret.push_back({p.first, p.second});
  }
  return ret;
}

void
RecNameToYaml::process_var_from_prefix(std::string prefix, RecNameToYaml::RecList &vars, bool &in_a_map)
{
  auto keys = find_all_keys_with_prefix(prefix, vars);

  for (auto &[rec, proccesed] : keys) {
    if (proccesed) {
      continue;
    }

    swoc::TextView name{std::get<0>(rec)};
    remove_legacy_config_prefix(name); // if no prefix to remove, it will ignore it.
    name.remove_prefix(prefix.size() + 1);

    auto const n = name.find(".");

    if (n == std::string_view::npos) {
      if (!in_a_map) {
        YAML_begin_map(doc); // Value map.
        in_a_map = true;
      }

      YAML_add_key(doc, std::string(name));

      auto const &rec_current_value = std::get<1>(rec);
      auto const &rec_default_value = std::get<2>(rec);
      YAML_add_value(doc, rec_current_value);
      if (include_defaults && !rec_default_value.empty()) {
        YAML_add_comment(doc, rec_default_value);
      }
      proccesed = true;
    } else {
      auto key = std::string{name.substr(0, n).data(), name.substr(0, n).size()};
      YAML_add_key(doc, key);

      std::string nprefix;
      nprefix.reserve(prefix.size() + 1 + key.size());
      nprefix.append(prefix);
      nprefix.append(".");
      nprefix.append(key);

      in_a_map = true; // no need for a map down the line if we will just add k,v.
      YAML_begin_map(doc);
      process_var_from_prefix(nprefix, vars, in_a_map);
      YAML_end_map(doc);
    }
  }
}

void
RecNameToYaml::build_yaml(RecNameToYaml::RecList vars)
{
  YAML_begin_map(doc);
  YAML_add_key(doc, "records");
  YAML_begin_map(doc); // content

  // Just work from every passed records and walk down every field to build up
  // each node from it.
  // It may work just to use a single loop, but this seems easier to follow, it will
  // not process the same records twice because once one is done, the processed mark
  // will be set and then ignored.
  for (auto &[rec, processed] : vars) {
    if (processed) {
      continue;
    }

    swoc::TextView name(std::get<0>(rec));
    remove_legacy_config_prefix(name);
    auto const  n = name.find('.');
    std::string prefix{name.substr(0, n == std::string_view::npos ? name.size() : n)};
    YAML_add_key(doc, prefix);
    if (n == std::string_view::npos) {
      auto const &rec_current_value = std::get<1>(rec);
      auto const &rec_default_value = std::get<2>(rec);
      // Just set this field right here and move on, no need to do it later.
      YAML_add_value(doc, rec_current_value);
      if (include_defaults && !rec_default_value.empty()) {
        YAML_add_comment(doc, rec_default_value);
      }
      processed = true;
      continue;
    }
    bool in_a_map = true;
    YAML_begin_map(doc);
    process_var_from_prefix(prefix, vars, in_a_map);
    YAML_end_map(doc);
  }

  YAML_end_map(doc); // content
  YAML_end_map(doc);
}
