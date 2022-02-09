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
#include "policy_manager.h"

PromotionPolicy *
PolicyManager::coalescePolicy(PromotionPolicy *policy)
{
  std::string tag = policy->id();

  if (tag.size() != 0) {
    auto res = _policies.find(tag);

    TSDebug(PLUGIN_NAME, "looking up policy by tag: %s", tag.c_str());

    if (res != _policies.end()) {
      TSDebug(PLUGIN_NAME, "repurposing policy for tag: %s", tag.c_str());

      ++res->second.second;
      // Repurpose the existing policy, nuking this placeholder.
      delete policy;
      return res->second.first;
    } else {
      TSDebug(PLUGIN_NAME, "inserting policy for tag: %s", tag.c_str());
      _policies[tag] = std::make_pair(policy, 1);
    }
  }

  return policy;
}

void
PolicyManager::releasePolicy(PromotionPolicy *policy)
{
  std::string tag = policy->id();

  if (tag.size() != 0) { // this is always the case for instances of LRUPolicy
    auto res = _policies.find(tag);

    if (res != _policies.end()) {
      if (0 == --res->second.second) {
        TSDebug(PLUGIN_NAME, "releasing unused PromotionPolicy");
        delete res->second.first;
        _policies.erase(res);
      }

      return;
    } else {
      TSDebug(PLUGIN_NAME, "Tried to release a policy which was not properly initialized nor acquired via PolicyManager");
    }
  }

  // Not managed by the policy manager, so just nuke it.
  delete policy;
}
