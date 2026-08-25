/** @file

  Virtual Host configuration implementation

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

#include <algorithm>
#include <cerrno>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <strings.h>
#include <sys/stat.h>
#include <yaml-cpp/yaml.h>

#include "proxy/VirtualHost.h"
#include "proxy/ReverseProxy.h"
#include "mgmt/config/ConfigRegistry.h"
#include "records/RecCore.h"
#include "tscore/Filenames.h"
#include "tsutil/Convert.h"

namespace
{
DbgCtl dbg_ctl_virtualhost("virtualhost");
}

int VirtualHost::_configid = 0;

std::string
VirtualHostConfig::Entry::get_id() const
{
  return id;
}

std::set<std::string> valid_vhost_keys = {"id", "domains", "remap"};

template <> struct YAML::convert<VirtualHostConfig::Entry> {
  static bool
  decode(const YAML::Node &node, VirtualHostConfig::Entry &item)
  {
    for (const auto &elem : node) {
      if (std::none_of(valid_vhost_keys.begin(), valid_vhost_keys.end(),
                       [&elem](const std::string &s) { return s == elem.first.as<std::string>(); })) {
        Warning("unsupported key '%s' in VirtualHost config", elem.first.as<std::string>().c_str());
      }
    }

    if (!node["id"]) {
      Error("Virtualhost entry at line %d must provide `id`", node.Mark().line + 1);
      return false;
    }
    item.id = node["id"].as<std::string>();

    auto domains = node["domains"];
    if (!domains || !domains.IsSequence() || domains.size() == 0) {
      Error("Virtualhost '%s' must provide at least one domain in a `domains` sequence (line %d)", item.id.c_str(),
            node.Mark().line + 1);
      return false;
    }
    item.exact_domains.clear();
    item.wildcard_domains.clear();

    for (const auto &it : domains) {
      auto domain_entry = it.as<std::string>();
      if (domain_entry.empty()) {
        Error("Virtualhost '%s' has an empty entry in `domains` (line %d)", item.id.c_str(), it.Mark().line + 1);
        return false;
      }
      char domain[TS_MAX_HOST_NAME_LEN + 1];
      ts::transform_lower(domain_entry, domain);

      // Check if domain is wildcard, prefixed with *
      if (domain[0] == '*') {
        if (domain[1] != '.' || domain[2] == '\0' || domain[2] == '.' || strchr(domain + 2, '*') != nullptr) {
          Error("Virtualhost '%s' wildcard '%s' must match '*.[domain]' format (line %d)", item.id.c_str(), domain,
                it.Mark().line + 1);
          return false;
        }
        item.wildcard_domains.emplace_back(domain + 2);
      } else {
        item.exact_domains.emplace_back(domain);
      }
    }

    if (item.exact_domains.empty() && item.wildcard_domains.empty()) {
      Error("Virtualhost '%s' must have at least one domain defined (line %d)", item.id.c_str(), node.Mark().line + 1);
      return false;
    }

    return true;
  }
};

bool
build_virtualhost_entry(YAML::Node const &node, Ptr<VirtualHostConfig::Entry> &entry)
{
  entry.clear();
  Ptr<VirtualHostConfig::Entry> vhost = make_ptr(new VirtualHostConfig::Entry);
  auto                         &conf  = *vhost;
  try {
    if (!YAML::convert<VirtualHostConfig::Entry>::decode(node, conf)) {
      return false;
    }
  } catch (YAML::Exception const &ex) {
    Error("Failed to parse virtualhost entry at line %d: %s", node.Mark().line + 1, ex.what());
    return false;
  }

  // Build UrlRewrite table for remap rules
  auto remap_node = node["remap"];
  if (remap_node) {
    auto table = std::make_unique<UrlRewrite>();
    table->set_remap_yaml(true);
    if (!table->load_table(conf.id, &remap_node)) {
      Error("Failed to load remap rules for virtualhost '%s' at line %d", conf.id.c_str(), remap_node.Mark().line + 1);
      return false;
    }
    conf.remap_table = make_managed_url_rewrite(std::move(table));
  }
  entry = std::move(vhost);
  return true;
}

bool
VirtualHostConfig::load()
{
  _entries.clear();
  _exact_domains_to_id.clear();
  _wildcard_domains_to_id.clear();
  std::string config_path = RecConfigReadConfigPath("proxy.config.virtualhost.filename", ts::filename::VIRTUALHOST);

  struct stat sbuf;
  if (stat(config_path.c_str(), &sbuf) == -1 && errno == ENOENT) {
    Warning("Virtualhost configuration '%s' doesn't exist", config_path.c_str());
    return true;
  }

  try {
    YAML::Node config = YAML::LoadFile(config_path);
    if (config.IsNull()) {
      Dbg(dbg_ctl_virtualhost, "Empty virtualhost config: %s", config_path.c_str());
      return true;
    }

    config = config["virtualhost"];
    if (config.IsNull() || !config.IsSequence()) {
      Error("%s: expected toplevel 'virtualhost' key to be a sequence", config_path.c_str());
      return false;
    }

    for (auto const &node : config) {
      Ptr<Entry> entry;
      if (!build_virtualhost_entry(node, entry)) {
        return false;
      }

      std::string vhost_id{entry->id};
      if (_entries.contains(vhost_id)) {
        Error("%s: duplicate virtualhost id '%s' (line %d)", config_path.c_str(), vhost_id.c_str(), node.Mark().line + 1);
        return false;
      }

      for (auto const &domain : entry->exact_domains) {
        if (_exact_domains_to_id.contains(domain)) {
          Error("%s: domain '%s' in virtualhost '%s' is already claimed by virtualhost '%s'", config_path.c_str(), domain.c_str(),
                vhost_id.c_str(), _exact_domains_to_id.at(domain).c_str());
          return false;
        }
        _exact_domains_to_id.emplace(domain, vhost_id);
      }

      for (auto const &domain_suffix : entry->wildcard_domains) {
        if (_wildcard_domains_to_id.contains(domain_suffix)) {
          Error("%s: wildcard domain '*.%s' in virtualhost '%s' is already claimed by virtualhost '%s'", config_path.c_str(),
                domain_suffix.c_str(), vhost_id.c_str(), _wildcard_domains_to_id.at(domain_suffix).c_str());
          return false;
        }
        _wildcard_domains_to_id.emplace(domain_suffix, vhost_id);
      }

      _entries.emplace(vhost_id, std::move(entry));
    }

  } catch (std::exception &ex) {
    Error("Failed to load %s: %s", config_path.c_str(), ex.what());
    return false;
  }
  return true;
}

bool
VirtualHostConfig::load_entry(std::string_view id, Ptr<Entry> &entry)
{
  entry.clear();
  std::string config_path = RecConfigReadConfigPath("proxy.config.virtualhost.filename", ts::filename::VIRTUALHOST);

  struct stat sbuf;
  if (stat(config_path.c_str(), &sbuf) == -1 && errno == ENOENT) {
    Warning("Virtualhost configuration '%s' doesn't exist", config_path.c_str());
    return false;
  }

  try {
    YAML::Node config = YAML::LoadFile(config_path);
    if (config.IsNull()) {
      Dbg(dbg_ctl_virtualhost, "Empty virtualhost config: %s", config_path.c_str());
      return false;
    }

    config = config["virtualhost"];
    if (config.IsNull() || !config.IsSequence()) {
      Error("%s: expected toplevel 'virtualhost' key to be a sequence", config_path.c_str());
      return false;
    }

    for (auto const &node : config) {
      auto config_id = node["id"];
      if (!config_id || config_id.as<std::string>() != id) {
        continue;
      }

      Ptr<Entry> vhost_entry;
      if (!build_virtualhost_entry(node, vhost_entry)) {
        return false;
      }
      entry = std::move(vhost_entry);
      return true;
    }

  } catch (std::exception &ex) {
    Error("Failed to load virtualhost entry '%.*s' in %s: %s", static_cast<int>(id.size()), id.data(), config_path.c_str(),
          ex.what());
    return false;
  }
  Error("%s: virtualhost with id '%.*s' not found", config_path.c_str(), static_cast<int>(id.size()), id.data());
  return false;
}

bool
VirtualHostConfig::set_entry(std::string_view id, Ptr<Entry> &entry)
{
  std::string vhost_id{id};
  // If virtualhost entry already exists, remove current entry
  if (auto it = _entries.find(vhost_id); it != _entries.end()) {
    Ptr<Entry> curr_entry = std::move(it->second);
    for (auto const &domain : curr_entry->exact_domains) {
      _exact_domains_to_id.erase(domain);
    }
    for (auto const &domain : curr_entry->wildcard_domains) {
      _wildcard_domains_to_id.erase(domain);
    }
    _entries.erase(vhost_id);
  }

  // Add new entry into virtualhost config
  if (entry) {
    for (auto const &domain : entry->exact_domains) {
      if (_exact_domains_to_id.contains(domain)) {
        Error("Domain '%s' in virtualhost '%s' is already claimed by virtualhost '%s'", domain.c_str(), vhost_id.c_str(),
              _exact_domains_to_id.at(domain).c_str());
        return false;
      }
      _exact_domains_to_id.emplace(domain, vhost_id);
    }

    for (auto const &domain_suffix : entry->wildcard_domains) {
      if (_wildcard_domains_to_id.contains(domain_suffix)) {
        Error("Wildcard domain '*.%s' in virtualhost '%s' is already claimed by virtualhost '%s'", domain_suffix.c_str(),
              vhost_id.c_str(), _wildcard_domains_to_id.at(domain_suffix).c_str());
        return false;
      }
      _wildcard_domains_to_id.emplace(domain_suffix, vhost_id);
    }

    _entries.emplace(vhost_id, std::move(entry));
  }
  return true;
}

Ptr<VirtualHostConfig::Entry>
VirtualHostConfig::find_by_id(std::string_view id) const
{
  if (_entries.empty()) {
    return Ptr<VirtualHostConfig::Entry>();
  }

  auto entry = _entries.find(std::string{id});
  if (entry != _entries.end()) {
    return entry->second;
  }
  return Ptr<VirtualHostConfig::Entry>();
}

Ptr<VirtualHostConfig::Entry>
VirtualHostConfig::find_by_domain(std::string_view domain) const
{
  if (_entries.empty() || domain.empty()) {
    return Ptr<VirtualHostConfig::Entry>();
  }

  char lower_domain[TS_MAX_HOST_NAME_LEN + 1];
  ts::transform_lower(std::string{domain}, lower_domain);

  // Check for exact match domains first
  auto id = _exact_domains_to_id.find(lower_domain);
  if (id != _exact_domains_to_id.end()) {
    auto entry = _entries.find(id->second);
    if (entry != _entries.end()) {
      return entry->second;
    }
  }

  // Check wildcard suffixes
  const char *subdomain = index(lower_domain, '.');
  while (subdomain) {
    subdomain++;
    if (auto suffix_id = _wildcard_domains_to_id.find(subdomain); suffix_id != _wildcard_domains_to_id.end()) {
      auto entry = _entries.find(suffix_id->second);
      if (entry != _entries.end()) {
        return entry->second;
      }
    }
    subdomain = index(subdomain, '.');
  }

  return Ptr<VirtualHostConfig::Entry>();
}

namespace
{
/** Reload handler for the `virtualhost` config.

    Registered as FileAndRpc so that `admin_config_reload` can carry `_reload` directives (currently
    just `id`, for a single-entry reload). Pushed config *content* is deliberately not supported:
    both reload paths re-read the on-disk file, so silently dropping a supplied body would report
    success for a change that never took effect.
 */
void
virtualhost_reload(ConfigContext ctx)
{
  ctx.in_progress();

  if (ctx.supplied_yaml()) {
    ctx.fail("virtualhost does not accept config content over rpc; only '_reload' directives are supported. "
             "Update " +
             std::string{ts::filename::VIRTUALHOST} + " and reload without a body.");
    return;
  }

  // Single-entry reload requested via -D virtualhost.id=<id>
  if (auto directives = ctx.reload_directives(); directives) {
    if (const auto id_dir = directives["id"]; id_dir) {
      if (!id_dir.IsScalar()) {
        ctx.fail("virtualhost '_reload' directive 'id' must be a scalar");
        return;
      }
      std::string id = id_dir.as<std::string>();
      if (VirtualHost::reconfigure(id)) {
        ctx.complete("Reloaded virtualhost entry: " + id);
      } else {
        ctx.fail("Failed to reload virtualhost entry: " + id);
      }
      return;
    }
  }

  if (VirtualHost::reconfigure()) {
    ctx.complete("Finished loading virtualhost config");
  } else {
    ctx.fail("Failed to load virtualhost config");
  }
}
} // namespace

void
VirtualHost::startup()
{
  if (!reconfigure()) {
    Fatal("failed to load %s", ts::filename::VIRTUALHOST);
  }
  RecRegisterConfigUpdateCb("proxy.config.virtualhost.filename", &VirtualHost::config_callback, nullptr);

  config::ConfigRegistry::Get_Instance().register_config(
    "virtualhost",                          // registry key
    ts::filename::VIRTUALHOST,              // default filename
    "proxy.config.virtualhost.filename",    // record holding the filename
    virtualhost_reload,                     // reload handler
    config::ConfigSource::FileAndRpc,       // rpc may supply '_reload' directives; content is rejected
    {"proxy.config.virtualhost.filename"}); // trigger records
}

int
VirtualHost::reconfigure()
{
  Note("%s loading ...", ts::filename::VIRTUALHOST);
  auto config = std::make_unique<VirtualHostConfig>();

  if (!config->load()) {
    Error("%s failed to load", ts::filename::VIRTUALHOST);
    return 0;
  }

  _configid = configProcessor.set(_configid, config.release());

  Note("%s finished loading", ts::filename::VIRTUALHOST);
  return 1;
}

int
VirtualHost::reconfigure(std::string_view id)
{
  VirtualHost::scoped_config vhost_config;
  Dbg(dbg_ctl_virtualhost, "Reconfiguring virtualhost entry: %s", id.data());
  // Reconfigure all vhosts if id not specified
  if (id.empty()) {
    Dbg(dbg_ctl_virtualhost, "No virtualhost specified, reconfiguring all entries");
    return reconfigure();
  }

  Ptr<VirtualHostConfig::Entry> entry;
  if (!VirtualHostConfig::load_entry(id, entry)) {
    return 0;
  }

  std::unique_ptr<VirtualHostConfig> config;
  if (vhost_config) {
    config = std::make_unique<VirtualHostConfig>(*vhost_config);
  } else {
    config = std::make_unique<VirtualHostConfig>();
  }

  if (!config->set_entry(id, entry)) {
    return 0;
  }
  _configid = configProcessor.set(_configid, config.release());
  return 1;
}

VirtualHostConfig *
VirtualHost::acquire()
{
  return static_cast<VirtualHostConfig *>(configProcessor.get(_configid));
}

void
VirtualHost::release(VirtualHostConfig *config)
{
  if (config && _configid > 0) {
    configProcessor.release(_configid, config);
  }
}

int
VirtualHost::config_callback(const char *, RecDataT, RecData, void *)
{
  eventProcessor.schedule_imm(new VirtualHostConfigContinuation, ET_TASK);
  return 0;
}
