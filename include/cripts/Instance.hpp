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

#include <array>
#include <variant>
#include <unordered_map>

#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Plugins.hpp"
#include "cripts/Metrics.hpp"
#include "cripts/Transaction.hpp"
#include "cripts/Bundle.hpp"

namespace Cript
{

class Instance
{
  using self_type = Instance;

public:
  using DataType = std::variant<integer, double, boolean, void *, Cript::string>;

  Instance()                        = delete;
  Instance(const self_type &)       = delete;
  void operator=(const self_type &) = delete;

  // This has to be in the .hpp file, otherwise we will not get the correct debug tag!
  Instance(int argc, char *argv[]) { initialize(argc, argv, __BASE_FILE__); }
  ~Instance()
  {
    plugins.clear();

    for (auto &bundle : bundles) {
      delete bundle;
    }
  }

  bool addPlugin(const Cript::string &tag, const Cript::string &plugin, const Plugin::Options &options);
  bool deletePlugin(const Cript::string &tag);
  void initialize(int argc, char *argv[], const char *filename);

  void
  addBundle(Cript::Bundle::Base *bundle)
  {
    for (auto &it : bundles) {
      if (it->name() == bundle->name()) {
        CFatal("[Instance]: Duplicate bundle %s", bundle->name().c_str());
      }
    }

    bundles.push_back(bundle);
  }

  // This allows Bundles to require hooks as well.
  void
  needCallback(Cript::Callbacks cb)
  {
    _callbacks |= cb;
  }

  void
  needCallback(unsigned cbs)
  {
    _callbacks |= cbs;
  }

  [[nodiscard]] unsigned
  callbacks() const
  {
    return _callbacks;
  }

  [[nodiscard]] bool
  debugOn() const
  {
    return dbg_ctl_cript.on();
  }

  void
  fail()
  {
    _failed = true;
  }

  [[nodiscard]] bool
  failed() const
  {
    return _failed;
  }

  [[nodiscard]] size_t
  size() const
  {
    return _size;
  }

  template <typename... T>
  void
  debug(fmt::format_string<T...> fmt, T &&...args) const
  {
    if (debugOn()) {
      auto str = fmt::vformat(fmt, fmt::make_format_args(args...));

      Dbg(dbg_ctl_cript, "%s", str.c_str());
    }
  }

  std::array<DataType, 32>                       data;
  Cript::string                                  to_url;
  Cript::string                                  from_url;
  Cript::string                                  plugin_debug_tag;
  std::unordered_map<std::string, Plugin::Remap> plugins;
  Cript::MetricStorage                           metrics{8};
  std::vector<Cript::Bundle::Base *>             bundles;

private:
  size_t   _size      = 0;
  bool     _failed    = false;
  unsigned _callbacks = 0;
  DbgCtl   dbg_ctl_cript;
}; // End class Instance

// A little wrapper / hack to make the do_create_instance take what looks like a context.
// This is only used during instantiation, not at runtime when the instance is used.
struct InstanceContext {
  using self_type = InstanceContext;

  InstanceContext()                  = delete;
  InstanceContext(const self_type &) = delete;
  void operator=(const self_type &)  = delete;

  InstanceContext(Cript::Instance &inst) : p_instance(inst) {}

  Cript::Instance &p_instance;
}; // End struct InstanceContext

} // namespace Cript
