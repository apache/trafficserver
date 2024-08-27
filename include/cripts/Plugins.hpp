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

#include "cripts/Lulu.hpp"

// This is not awesome, but these APIs are internal to ATS. Once Cripts becomes
// part of the ATS core, we can remove this.

class RemapPluginInst; // Opaque to the Cripts, but needed in the implementation.

namespace cripts::Plugin
{
using Options = std::vector<cripts::string>;

class Remap
{
  using self_type = Remap;

public:
  Remap()                                 = default;
  Remap(const self_type &)                = delete;
  self_type &operator=(const self_type &) = delete;
  self_type &operator=(self_type &&)      = default;

  Remap(Remap &&other) noexcept
  {
    _plugin       = other._plugin;
    _valid        = other._valid;
    other._plugin = nullptr;
  }

  ~Remap() { Cleanup(); }

  void _runRemap(cripts::Context *context);

  void Cleanup();

  [[nodiscard]] bool
  Valid() const
  {
    return _valid;
  }

  // Factory, sort of
  static Remap Create(const std::string &tag, const std::string &plugin, const cripts::string &from_url,
                      const cripts::string &to_url, const Options &options);

  static void Initialize();

private:
  RemapPluginInst *_plugin = nullptr;
  bool             _valid  = false;
}; // End class Plugin::Remap

} // namespace cripts::Plugin
