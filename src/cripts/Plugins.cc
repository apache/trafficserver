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

#include "proxy/http/remap/PluginFactory.h"
#include <swoc/swoc_file.h>

#include "cripts/Preamble.hpp"

namespace Cript
{

// This is global for all Cripts, and it will not get reloaded on a config reload.
PluginFactory gPluginFactory;

void
Plugin::Remap::Initialize()
{
  gPluginFactory.setRuntimeDir(RecConfigReadRuntimeDir()).addSearchDir(RecConfigReadPluginDir());
}

Plugin::Remap
Plugin::Remap::Create(const std::string &tag, const std::string &plugin, const Cript::string &from_url, const Cript::string &to_url,
                      const Plugin::Options &options)
{
  Plugin::Remap          inst;
  int                    argc = options.size() + 1;
  const char           **argv = new const char *[argc];
  const swoc::file::path path(plugin);

  // Remap plugins expect the first two arguments to be the from and to URLs.
  argv[0] = from_url.c_str();
  argv[1] = to_url.c_str();

  for (unsigned i = 0; i < options.size(); ++i) {
    argv[i + 2] = options[i].c_str();
  }

  std::string error;

  // We have to escalate access while loading these plugins, just as done when loading remap.config
  {
    uint32_t elevate_access = 0;

    REC_ReadConfigInteger(elevate_access, "proxy.config.plugin.load_elevated");
    ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

    inst._plugin = gPluginFactory.getRemapPlugin(path, argc, const_cast<char **>(argv), error, isPluginDynamicReloadEnabled());
  } // done elevating access

  delete[] argv;

  if (!inst._plugin) {
    TSError("[%s] Unable to load plugin '%s': %s", tag.c_str(), plugin.c_str(), error.c_str());
    inst._valid = false;
  } else {
    inst._valid = true;
  }

  return inst; // RVO
}

void
Plugin::Remap::Cleanup()
{
  if (_plugin) {
    _plugin->done();
    _plugin = nullptr;
  }
}

void
Plugin::Remap::_runRemap(Cript::Context *context)
{
  _plugin->doRemap(context->state.txnp, context->rri);
}

} // namespace Cript
