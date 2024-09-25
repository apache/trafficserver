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

#include "ts/ts.h"

#include "cripts/Lulu.hpp"
#include "cripts/Instance.hpp"

namespace cripts
{

void
Instance::_initialize(int argc, const char *argv[], const char *filename, bool remap)
{
  if (remap) {
    from_url = argv[0];
    to_url   = argv[1];
    for (int i = 2; i < argc; i++) {
      auto s = cripts::string(argv[i]);

      s.trim("\"\'");
      data[i - 2] = s;
    }
    _size = argc - 2;
  }

  // Set the debug tag for this plugin, slightly annoying that we have to calculate
  // this for every instantiation, but in general, each Cript is used primarily for
  // one single remap rule
  plugin_debug_tag = filename;
  auto slash       = plugin_debug_tag.find_last_of('/');
  auto period      = plugin_debug_tag.find_last_of('.');

  if (slash == cripts::string::npos) {
    slash = 0;
  }

  if (period != cripts::string::npos) {
    plugin_debug_tag = std::string_view(plugin_debug_tag.substr(slash + 1, period - slash - 1));
  }

  dbg_ctl_cript.set(plugin_debug_tag.c_str());
}

bool
Instance::AddPlugin(const cripts::string &tag, const cripts::string &plugin, const Plugin::Options &options)
{
  if (plugins.find(tag) != plugins.end()) {
    return false;
  }

  auto p = Plugin::Remap::Create(tag, plugin, from_url, to_url, options);

  if (p.Valid()) {
    plugins.emplace(tag, std::move(p));

    return true;
  } else {
    Fail();
    return false;
  }
}

bool
Instance::DeletePlugin(const cripts::string &tag)
{
  auto p = plugins.find(tag);

  if (p != plugins.end()) {
    plugins.erase(p);

    return true;
  } else {
    return false;
  }
}

void
Instance::AddBundle(cripts::Bundle::Base *bundle)
{
  for (auto &it : bundles) {
    if (it->Name() == bundle->Name()) {
      CFatal("[Instance]: Duplicate bundle %s", bundle->Name().c_str());
    }
  }

  bundles.push_back(bundle);
}

} // namespace cripts
