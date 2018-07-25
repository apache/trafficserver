/** @file
 *
 *  Remap configuration file parsing.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fstream>
#include <sstream>
#include <string.h>
#include <RemapParentConfig.h>

#include "ts/Diags.h"

bool
RemapParentConfig::parse(const char *filename)
{
  bool result = true;

  try {
    result = loadConfig(filename);
  } catch (std::exception &ex) {
    Error("%s", ex.what());
    result = false;
  }

  if (result) {
    std::string str_policy;
    std::string hashkey;
    YAML::Node strategy = config["strategy"];
    if (strategy.Type() == YAML::NodeType::Map) {
      // parse and save the selection strategy policy.
      try {
        str_policy = strategy["policy"].as<std::string>();
      } catch (std::exception &ex) {
        Error("yaml parse error, no selection strategy policy is defined in %s", filename);
        return false;
      }
      setPolicy(str_policy);
      if (selection_policy_type == SelectionStrategyPolicy::POLICY_UNDEFINED) {
        Error("selection policy lookup failed for policytype: %s", str_policy.c_str());
        return false;
      }
      // parse and save the hash_key type.
      // hash_key is optional.
      try {
        hashkey = strategy["hash_key"].as<std::string>();
      } catch (std::exception &ex) {
        ;
      }
      if (!hashkey.empty()) {
        setHashKeyType(hashkey);
        if (hash_key_type == SelectionStrategyHashKeyType::HASH_UNDEFINED) {
          Error("hash_key lookup failed for hash_key: %s", hashkey.c_str());
          return false;
        }
      }
    } else {
      Error("yaml parse error, no strategy section found in %s, expecting a 'strategy' map", filename);
      return false;
    }
  }

  return result;
}

bool
RemapParentConfig::loadConfig(const char *filename)
{
  bool result     = true;
  const char *sep = " \t";
  std::stringstream buf;
  char mline[8192], iline[8192];
  char *word, *last;

  std::ifstream file(filename);
  if (file.is_open()) {
    while (file.getline(mline, 8191)) {
      buf << mline << "\n";
      word = strtok_r(mline, sep, &last);
      // load an include file
      if (word != nullptr && strncmp("#include", word, 8) == 0) {
        word = strtok_r(nullptr, sep, &last);
        if (word != nullptr) {
          std::ifstream ifile(word);
          if (ifile.is_open()) {
            while (ifile.getline(iline, 8191)) {
              buf << iline << "\n";
            }
            ifile.close();
          } else {
            Error("unable to open %s", word);
            result = false;
            break;
          }
        }
      }
    }
    file.close();
  } else {
    Error("unable to open %s", filename);
    result = false;
  }

  if (result) {
    config = YAML::Load(buf);

    if (config.IsNull()) {
      Error("%s is empty", filename);
      result = false;
    }

    if (!config.IsMap()) {
      Error("malformed %s file; expected a map.", filename);
      result = false;
    }
  }

  return result;
}

SelectionStrategyHashKeyType
RemapParentConfig::setHashKeyType(std::string &key)
{
  auto it = hash_key_types.find(key);
  if (it == hash_key_types.end()) {
    hash_key_type = SelectionStrategyHashKeyType::HASH_UNDEFINED;
  } else {
    hash_key_type = it->second;
  }
  return hash_key_type;
}

SelectionStrategyPolicy
RemapParentConfig::setPolicy(std::string &key)
{
  auto it = selection_policy_types.find(key);
  if (it == selection_policy_types.end()) {
    selection_policy_type = SelectionStrategyPolicy::POLICY_UNDEFINED;
  } else {
    selection_policy_type = it->second;
  }
  return selection_policy_type;
}
