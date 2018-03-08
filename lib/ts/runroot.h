/** @file

  A brief file prefix

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

// runroot handler for TS_RUNROOT
// detailed information in runroot.cc

#pragma once

#include <string>
#include <unordered_map>

std::string check_parent_path(const std::string &path);

void runroot_handler(const char **argv, bool json = false);

// get runroot map from yaml path and prefix
std::unordered_map<std::string, std::string> runroot_map(std::string &yaml_path, std::string &prefix);

// help check runroot for layout
std::unordered_map<std::string, std::string> check_runroot();
