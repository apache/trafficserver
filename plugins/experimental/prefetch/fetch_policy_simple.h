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

/**
 * @file  fetch_policy_simple.h
 * @brief Simple fetch policy (header file).
 */

#pragma once

#include "fetch_policy.h"

/**
 *  @brief Simple de-duplication fetch policy, used to make sure only one background fetch is running at a time.
 */

class FetchPolicySimple : public FetchPolicy
{
public:
  FetchPolicySimple() {}
  virtual ~FetchPolicySimple(){};
  bool init(const char *parameters);
  bool acquire(const std::string &url);
  bool release(const std::string &url);
  const char *name();
  size_t getSize();
  size_t getMaxSize();

private:
  std::unordered_map<std::string, bool> _urls;
};
