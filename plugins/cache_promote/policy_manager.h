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
#pragma once

#include <atomic>
#include <unordered_map>

#include "policy.h"

class PolicyManager
{
public:
  PolicyManager() { TSDebug(PLUGIN_NAME, "PolicyManager() CTOR"); }
  virtual ~PolicyManager() { TSDebug(PLUGIN_NAME, "~PolicyManger() DTOR"); }

  // This is sort of a no-op right now, but it should be called regardless.
  void
  clear()
  {
    // This should always be zero here, otherwise, we have not released policies properly.
    TSReleaseAssert(_policies.size() == 0);
  }

  // This is the main entry point.
  PromotionPolicy *coalescePolicy(PromotionPolicy *policy);
  void releasePolicy(PromotionPolicy *policy);

  // Don't allow copy-constructors.
  PolicyManager(PolicyManager const &) = delete;
  void operator=(PolicyManager const &) = delete;

private:
  std::unordered_map<std::string, std::pair<PromotionPolicy *, std::atomic<unsigned>>> _policies;
};
