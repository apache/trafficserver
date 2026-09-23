/** @file
  Virtual Host configuration
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

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "iocore/eventsystem/ConfigProcessor.h"
#include "mgmt/config/ConfigContext.h"
#include "proxy/http/remap/UrlRewrite.h"
#include "tscore/Ptr.h"

class VirtualHostPluginReload;

class VirtualHostConfig : public ConfigInfo
{
public:
  VirtualHostConfig() = default;
  VirtualHostConfig(const VirtualHostConfig &other)
    : _entries(other._entries),
      _exact_domains_to_id(other._exact_domains_to_id),
      _wildcard_domains_to_id(other._wildcard_domains_to_id)
  {
  }
  VirtualHostConfig &
  operator=(const VirtualHostConfig &other)
  {
    if (this != &other) {
      _entries                = other._entries;
      _exact_domains_to_id    = other._exact_domains_to_id;
      _wildcard_domains_to_id = other._wildcard_domains_to_id;
    }
    return *this;
  }
  ~VirtualHostConfig() = default;

  struct Entry : public RefCountObjInHeap {
    std::string                 id;
    std::vector<std::string>    exact_domains;
    std::vector<std::string>    wildcard_domains;
    std::shared_ptr<UrlRewrite> remap_table;

    std::string get_id() const;
  };

  /** Load every entry from the configuration file into this (empty) config.

      @param initial_load Set only for the load performed by @c VirtualHost::startup(). An absent
      or empty file is a supported "no virtualhosts configured" state there, but on a reload it is
      an error: reporting success would publish an empty config over a live routing table, silently
      dropping every per-domain remap table.
   */
  bool        load(ConfigContext ctx = {}, bool initial_load = false, VirtualHostPluginReload *plugin_reload = nullptr);
  bool        set_entry(std::string_view id, Ptr<Entry> &entry, ConfigContext ctx = {});
  static bool load_entry(std::string_view id, Ptr<Entry> &entry, ConfigContext ctx = {},
                         VirtualHostPluginReload *plugin_reload = nullptr);
  Ptr<Entry>  find_by_id(std::string_view id) const;
  Ptr<Entry>  find_by_domain(std::string_view domain) const;

  /// Add the remap plugins instantiated by every entry's remap table to @a used.
  void collect_used_plugins(std::unordered_map<PluginDso *, int> &used) const;

  size_t
  entry_count() const
  {
    return _entries.size();
  }

private:
  using entry_map = std::unordered_map<std::string, Ptr<Entry>>;
  using name_map  = std::unordered_map<std::string, std::string>;

  entry_map _entries;
  name_map  _exact_domains_to_id;
  name_map  _wildcard_domains_to_id;
};

/** Sends one remap plugin reload notification pair for a whole virtualhost rebuild.

    The pre/post callbacks go to every loaded remap plugin, not just the ones a table uses, so
    notifying per table would repeat them for each entry and report plugins used only by earlier
    tables as unused. The pre notification is sent lazily, before the first remap table is built, so
    a rebuild with no remap tables sends nothing. If @c finish() is never reached, the destructor
    reports the reload as failed.
 */
class VirtualHostPluginReload
{
public:
  VirtualHostPluginReload()                                           = default;
  VirtualHostPluginReload(const VirtualHostPluginReload &)            = delete;
  VirtualHostPluginReload &operator=(const VirtualHostPluginReload &) = delete;
  ~VirtualHostPluginReload();

  /// Call before building a remap table.
  void begin();
  /// Report success, with @a config as the full set of tables that will be live.
  void finish(VirtualHostConfig const &config);

private:
  bool _started = false;
};

class VirtualHost
{
public:
  using scoped_config = ConfigProcessor::scoped_config<VirtualHost, VirtualHostConfig>;

  static void               startup();
  static int                reconfigure(ConfigContext ctx = {}, bool initial_load = false);
  static int                reconfigure(std::string_view id, ConfigContext ctx = {});
  static VirtualHostConfig *acquire();
  static void               release(VirtualHostConfig *config);

private:
  static std::atomic<int> _configid;
};
