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

// Factory functions to create/destroy RulesConfig without exposing the full definition.
// This avoids ODR violations between main.cc and the library.

#include "rules_config.h"

// Factory function implementations
extern "C" {

RulesConfig *
create_rules_config(int timezone, int inboundIpSource)
{
  return new RulesConfig(timezone, inboundIpSource);
}

void
destroy_rules_config(RulesConfig *conf)
{
  delete conf;
}

bool
rules_config_parse(RulesConfig *conf, const char *fname, int default_hook, char *from_url, char *to_url, bool force_hrw4u)
{
  return conf->parse_config(fname, static_cast<TSHttpHookID>(default_hook), from_url, to_url, force_hrw4u);
}

RuleSet *
rules_config_get_rule(RulesConfig *conf, int hook)
{
  return conf->rule(hook);
}

} // extern "C"
