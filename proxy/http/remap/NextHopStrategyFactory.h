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

#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "tscore/Diags.h"
#include "NextHopSelectionStrategy.h"

namespace YAML
{
class Node;
};

class NextHopStrategyFactory
{
public:
  NextHopStrategyFactory() = delete;
  NextHopStrategyFactory(const char *file);
  ~NextHopStrategyFactory();
  std::shared_ptr<NextHopSelectionStrategy> strategyInstance(const char *name);

  bool strategies_loaded;

private:
  std::string fn;
  void loadConfigFile(const std::string &file, std::stringstream &doc, std::unordered_set<std::string> &include_once);
  void createStrategy(const std::string &name, const NHPolicyType policy_type, const YAML::Node &node);
  std::unordered_map<std::string, std::shared_ptr<NextHopSelectionStrategy>> _strategies;
};
