/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "utilities.h"
#include "lists.h"

bool
List::IP::parseYaml(const YAML::Node &node)
{
  const YAML::Node &cidr = node["cidr"];

  if (cidr && cidr.IsSequence()) {
    for (size_t i = 0; i < cidr.size(); ++i) {
      std::string str = cidr[i].as<std::string>();

      Dbg(dbg_ctl, "Adding CIDR %s to List %s", str.c_str(), _name.c_str());
      add(str);
    }
  } else {
    TSError("[%s] No 'cidr' list found in Lists rule %s", PLUGIN_NAME, name().c_str());
    return false;
  }

  return true;
}
