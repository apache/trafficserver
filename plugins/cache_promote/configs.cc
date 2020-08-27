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
#include <getopt.h>

#include "configs.h"
#include "lru_policy.h"
#include "chance_policy.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// ToDo: It's ugly that this is a "global" options list, clearly each policy should be able
// to add to this list, making them more modular.
static const struct option longopt[] = {
  {const_cast<char *>("policy"), required_argument, nullptr, 'p'},
  // This is for both Chance and LRU (optional) policy
  {const_cast<char *>("sample"), required_argument, nullptr, 's'},
  // For the LRU policy
  {const_cast<char *>("buckets"), required_argument, nullptr, 'b'},
  {const_cast<char *>("hits"), required_argument, nullptr, 'h'},
  {const_cast<char *>("stats-enable-with-id"), required_argument, nullptr, 'e'},
  {const_cast<char *>("label"), required_argument, nullptr, 'l'},
  // EOF
  {nullptr, no_argument, nullptr, '\0'},
};

// The destructor is responsible for returning the policy to the PolicyManager.
PromotionConfig::~PromotionConfig()
{
  _manager->releasePolicy(_policy);
}

// Parse the command line arguments to the plugin, and instantiate the appropriate policy
bool
PromotionConfig::factory(int argc, char *argv[])
{
  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "", longopt, nullptr);

    if (opt == -1) {
      break;
    } else if (opt == 'p') {
      if (0 == strncasecmp(optarg, "chance", 6)) {
        _policy = new ChancePolicy();
      } else if (0 == strncasecmp(optarg, "lru", 3)) {
        _policy = new LRUPolicy();
      } else {
        TSError("[%s] Unknown policy --policy=%s", PLUGIN_NAME, optarg);
        return false;
      }
      if (_policy) {
        TSDebug(PLUGIN_NAME, "created remap with cache promotion policy = %s", _policy->policyName());
      }
    } else if (opt == 'e') {
      if (optarg == nullptr) {
        TSError("[%s] the -%c option requires an argument, the remap identifier.", PLUGIN_NAME, opt);
        return false;
      } else {
        if (_policy && _policy->stats_add(optarg)) {
          _policy->stats_enabled = true;
          TSDebug(PLUGIN_NAME, "stats collection is enabled");
        }
      }
    } else {
      if (_policy) {
        // The --sample (-s) option is allowed for all configs, but only after --policy is specified.
        if (opt == 's') {
          _policy->setSample(optarg);
        } else {
          if (!_policy->parseOption(opt, optarg)) {
            TSError("[%s] The specified policy (%s) does not support the -%c option", PLUGIN_NAME, _policy->policyName(), opt);
            delete _policy;
            _policy = nullptr;
            return false;
          }
        }
      } else {
        TSError("[%s] The --policy=<n> parameter must come first on the remap configuration", PLUGIN_NAME);
        return false;
      }
    }
  }

  // Coalesce any LRU policies via the LRU manager. This is a little ugly, but it makes configuration
  // easier, and order of options doesn't matter.

  // This can return the same policy, or an existing one, in which case, this one is deleted by the Manager
  _policy = _manager->coalescePolicy(_policy);

  return true;
}
