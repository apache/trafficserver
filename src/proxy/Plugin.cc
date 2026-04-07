/** @file

  Plugin init

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

#include <cstdio>
#include <algorithm>
#include <optional>
#include "tscore/ink_platform.h"
#include "tscore/ink_file.h"
#include "tscore/ParseRules.h"
#include "records/RecCore.h"
#include "tscore/Layout.h"
#include "proxy/Plugin.h"
#include "tscore/ink_cap.h"
#include "tscore/Filenames.h"
#include <yaml-cpp/yaml.h>

#define MAX_PLUGIN_ARGS 64

static PluginDynamicReloadMode plugin_dynamic_reload_mode = PluginDynamicReloadMode::ON;

bool
isPluginDynamicReloadEnabled()
{
  return PluginDynamicReloadMode::ON == plugin_dynamic_reload_mode;
}

void
enablePluginDynamicReload()
{
  plugin_dynamic_reload_mode = PluginDynamicReloadMode::ON;
}

void
disablePluginDynamicReload()
{
  plugin_dynamic_reload_mode = PluginDynamicReloadMode::OFF;
}

void
parsePluginDynamicReloadConfig()
{
  int int_plugin_dynamic_reload_mode;

  int_plugin_dynamic_reload_mode = RecGetRecordInt("proxy.config.plugin.dynamic_reload_mode").value_or(0);
  plugin_dynamic_reload_mode     = static_cast<PluginDynamicReloadMode>(int_plugin_dynamic_reload_mode);

  if (static_cast<int>(plugin_dynamic_reload_mode) < 0 ||
      static_cast<int>(plugin_dynamic_reload_mode) >= static_cast<int>(PluginDynamicReloadMode::COUNT)) {
    Warning("proxy.config.plugin.dynamic_reload_mode out of range. using default value.");
    plugin_dynamic_reload_mode = PluginDynamicReloadMode::ON;
  }
  Note("Initialized plugin_dynamic_reload_mode: %d", static_cast<int>(plugin_dynamic_reload_mode));
}

void
parsePluginConfig()
{
  parsePluginDynamicReloadConfig();
}

static const char *plugin_dir = ".";

static void
plugin_dir_init()
{
  static bool once = true;

  if (once) {
    plugin_dir = ats_stringdup(RecConfigReadPluginDir());
    once       = false;
  }
}

using init_func_t = void (*)(int, char **);

static PluginLoadSummary s_plugin_load_summary;

const PluginLoadSummary &
get_plugin_load_summary()
{
  return s_plugin_load_summary;
}

// Plugin registration vars
//
//    plugin_reg_list has an entry for each plugin
//      we've successfully been able to load
//    plugin_reg_current is used to associate the
//      plugin we're in the process of loading with
//      it struct.  We need this global pointer since
//      the API doesn't have any plugin context.  Init
//      is single threaded so we can get away with the
//      global pointer
//
DLL<PluginRegInfo> plugin_reg_list;
PluginRegInfo     *plugin_reg_current = nullptr;

PluginRegInfo::PluginRegInfo() = default;

PluginRegInfo::~PluginRegInfo()
{
  // We don't support unloading plugins once they are successfully loaded, so assert
  // that we don't accidentally attempt this.
  ink_release_assert(this->plugin_registered == false);
  ink_release_assert(this->link.prev == nullptr);

  ats_free(this->plugin_path);
  ats_free(this->plugin_name);
  ats_free(this->vendor_name);
  ats_free(this->support_email);
  if (dlh) {
    dlclose(dlh);
  }
}

bool
plugin_dso_load(const char *path, void *&handle, void *&init, std::string &error)
{
  handle = dlopen(path, RTLD_NOW);
  init   = nullptr;
  if (!handle) {
    error.assign("unable to load '").append(path).append("': ").append(dlerror());
    Error("%s", error.c_str());
    return false;
  }

  init = dlsym(handle, "TSPluginInit");
  if (!init) {
    error.assign("unable to find TSPluginInit function in '").append(path).append("': ").append(dlerror());
    Error("%s", error.c_str());
    dlclose(handle);
    handle = nullptr;
    return false;
  }

  return true;
}

bool
single_plugin_init(int argc, char *argv[], bool validateOnly)
{
  char        path[PATH_NAME_MAX];
  init_func_t init;

  if (argc < 1) {
    return true;
  }
  ink_filepath_make(path, sizeof(path), plugin_dir, argv[0]);

  Note("loading plugin '%s'", path);

  for (PluginRegInfo *plugin_reg_temp = plugin_reg_list.head; plugin_reg_temp != nullptr;
       plugin_reg_temp                = (plugin_reg_temp->link).next) {
    if (strcmp(plugin_reg_temp->plugin_path, path) == 0) {
      Warning("multiple loading of plugin %s", path);
      break;
    }
  }

  // elevate the access to read files as root if compiled with capabilities, if not
  // change the effective user to root
  {
    uint32_t elevate_access = 0;
    elevate_access          = RecGetRecordInt("proxy.config.plugin.load_elevated").value_or(0);
    ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

    void       *handle, *initptr = nullptr;
    std::string error;
    bool        loaded = plugin_dso_load(path, handle, initptr, error);
    init               = reinterpret_cast<init_func_t>(initptr);

    if (!loaded) {
      if (validateOnly) {
        return false;
      }
      Fatal("%s", error.c_str());
      return false; // this line won't get called since Fatal brings down ATS
    }

    // Allocate a new registration structure for the
    //    plugin we're starting up
    ink_assert(plugin_reg_current == nullptr);
    plugin_reg_current              = new PluginRegInfo;
    plugin_reg_current->plugin_path = ats_strdup(path);
    plugin_reg_current->dlh         = handle;

#if (!defined(kfreebsd) && defined(freebsd)) || defined(darwin)
    optreset = 1;
#endif
#if defined(__GLIBC__)
    optind = 0;
#else
    optind = 1;
#endif
    opterr = 0;
    optarg = nullptr;
    init(argc, argv);
  } // done elevating access

  if (plugin_reg_current->plugin_registered) {
    plugin_reg_list.push(plugin_reg_current);
  } else {
    Fatal("plugin '%s' not registered by calling TSPluginRegister", path);
    return false; // this line won't get called since Fatal brings down ATS
  }

  plugin_reg_current = nullptr;
  Note("plugin '%s' finished loading", path);
  return true;
}

char *
plugin_expand(char *arg)
{
  RecDataT data_type;
  char    *str = nullptr;

  if (*arg != '$') {
    return nullptr;
  }
  // skip the $ character
  arg += 1;

  if (RecGetRecordDataType(arg, &data_type) != REC_ERR_OKAY) {
    goto not_found;
  }

  switch (data_type) {
  case RECD_STRING: {
    auto rec_str{RecGetRecordStringAlloc(arg)};
    if (!rec_str) {
      goto not_found;
    }
    return ats_stringdup(rec_str);
    break;
  }
  case RECD_FLOAT: {
    auto float_val{RecGetRecordFloat(arg)};
    if (!float_val) {
      goto not_found;
    }
    str = static_cast<char *>(ats_malloc(128));
    snprintf(str, 128, "%f", static_cast<float>(float_val.value()));
    return str;
    break;
  }
  case RECD_INT: {
    auto int_val{RecGetRecordInt(arg)};
    if (!int_val) {
      goto not_found;
    }
    str = static_cast<char *>(ats_malloc(128));
    snprintf(str, 128, "%ld", static_cast<long int>(int_val.value()));
    return str;
    break;
  }
  case RECD_COUNTER: {
    auto count_val{RecGetRecordCounter(arg)};
    if (!count_val) {
      goto not_found;
    }
    str = static_cast<char *>(ats_malloc(128));
    snprintf(str, 128, "%ld", static_cast<long int>(count_val.value()));
    return str;
    break;
  }
  default:
    goto not_found;
    break;
  }

not_found:
  Warning("%s: unable to find parameter %s", ts::filename::PLUGIN, arg);
  return nullptr;
}

bool
plugin_init(bool validateOnly)
{
  ats_scoped_str path;
  char           line[1024], *p;
  char          *argv[MAX_PLUGIN_ARGS];
  char          *vars[MAX_PLUGIN_ARGS];
  int            argc;
  int            fd;
  int            i;
  bool           retVal     = true;
  int            load_index = 0;

  plugin_dir_init();

  s_plugin_load_summary.source = ts::filename::PLUGIN;
  s_plugin_load_summary.entries.clear();

  Note("%s loading ...", ts::filename::PLUGIN);
  path = RecConfigReadConfigPath(nullptr, ts::filename::PLUGIN);
  fd   = open(path, O_RDONLY);
  if (fd < 0) {
    Warning("%s failed to load: %d, %s", ts::filename::PLUGIN, errno, strerror(errno));
    return false;
  }

  while (ink_file_fd_readline(fd, sizeof(line) - 1, line) > 0) {
    argc = 0;
    p    = line;

    // strip leading white space and test for comment or blank line
    while (*p && ParseRules::is_wslfcr(*p)) {
      ++p;
    }
    if ((*p == '\0') || (*p == '#')) {
      continue;
    }

    // not comment or blank, so rip line into tokens
    while (true) {
      if (argc >= MAX_PLUGIN_ARGS) {
        Warning("Exceeded max number of args (%d) for plugin: [%s]", MAX_PLUGIN_ARGS, argc > 0 ? argv[0] : "???");
        break;
      }

      while (*p && ParseRules::is_wslfcr(*p)) {
        ++p;
      }
      if ((*p == '\0') || (*p == '#')) {
        break; // EOL
      }

      if (*p == '\"') {
        p += 1;

        argv[argc++] = p;

        while (*p && (*p != '\"')) {
          p += 1;
        }
        if (*p == '\0') {
          break;
        }
        *p++ = '\0';
      } else {
        argv[argc++] = p;

        while (*p && !ParseRules::is_wslfcr(*p) && (*p != '#')) {
          p += 1;
        }
        if ((*p == '\0') || (*p == '#')) {
          break;
        }
        *p++ = '\0';
      }
    }

    for (i = 0; i < argc; i++) {
      vars[i] = plugin_expand(argv[i]);
      if (vars[i]) {
        argv[i] = vars[i];
      }
    }

    if (argc < MAX_PLUGIN_ARGS) {
      argv[argc] = nullptr;
    } else {
      argv[MAX_PLUGIN_ARGS - 1] = nullptr;
    }

    ++load_index;
    std::string plugin_name = (argc > 0) ? argv[0] : "unknown";

    retVal = single_plugin_init(argc, argv, validateOnly);

    s_plugin_load_summary.entries.push_back({plugin_name, -1, true, retVal, load_index});

    for (i = 0; i < argc; i++) {
      ats_free(vars[i]);
    }
  }

  close(fd);
  if (retVal) {
    Note("%s finished loading", ts::filename::PLUGIN);
  } else {
    Error("%s failed to load", ts::filename::PLUGIN);
  }
  return retVal;
}

config::ConfigResult<PluginYAMLEntries>
parse_plugin_yaml(const char *yaml_path)
{
  config::ConfigResult<PluginYAMLEntries> result;
  YAML::Node                              root;

  try {
    root = YAML::LoadFile(yaml_path);
  } catch (const YAML::Exception &e) {
    result.errata.note("failed to parse: {}", e.what());
    return result;
  }

  if (!root["plugins"] || !root["plugins"].IsSequence()) {
    result.errata.note("missing or invalid 'plugins' sequence");
    return result;
  }

  struct IndexedEntry {
    int             seq_idx;
    PluginYAMLEntry entry;
  };

  std::vector<IndexedEntry> indexed;
  int                       seq_idx = 0;

  for (const auto &node : root["plugins"]) {
    PluginYAMLEntry entry;

    if (!node["path"]) {
      result.errata.note("plugin entry #{} missing required 'path' field", seq_idx + 1);
      return result;
    }
    entry.path = node["path"].as<std::string>();

    if (auto n = node["enabled"]; n) {
      entry.enabled = n.as<bool>();
    }
    if (auto n = node["load_order"]; n) {
      entry.load_order = n.as<int>();
    }
    if (auto n = node["params"]; n && n.IsSequence()) {
      for (const auto &p : n) {
        entry.params.emplace_back(p.as<std::string>());
      }
    }

    indexed.push_back({seq_idx++, std::move(entry)});
  }

  std::stable_sort(indexed.begin(), indexed.end(), [](const IndexedEntry &a, const IndexedEntry &b) {
    const bool a_has = a.entry.load_order >= 0;
    const bool b_has = b.entry.load_order >= 0;

    if (a_has && b_has) {
      return a.entry.load_order < b.entry.load_order;
    }
    return a_has && !b_has;
  });

  result.value.reserve(indexed.size());
  for (auto &[_, entry] : indexed) {
    result.value.emplace_back(std::move(entry));
  }

  return result;
}

/// Build the argv for a single plugin: [path, params..., $record expansions].
static std::optional<std::vector<std::string>>
build_plugin_args(const PluginYAMLEntry &entry)
{
  std::vector<std::string> args;
  args.emplace_back(entry.path);

  for (const auto &p : entry.params) {
    args.emplace_back(p);
  }

  return args;
}

static void
log_plugin_load_summary(int loaded, int disabled)
{
  Note("%s: %d plugins loaded, %d disabled", ts::filename::PLUGIN_YAML, loaded, disabled);

  for (const auto &e : s_plugin_load_summary.entries) {
    if (e.enabled) {
      if (e.load_order >= 0) {
        Note("  #%d %-30s load_order: %-5d loaded", e.index, e.path.c_str(), e.load_order);
      } else {
        Note("  #%d %-30s                 loaded", e.index, e.path.c_str());
      }
    } else {
      Note("  -- %-30s                 disabled", e.path.c_str());
    }
  }
}

bool
plugin_yaml_init(bool validateOnly)
{
  plugin_dir_init();

  ats_scoped_str yaml_path;

  yaml_path = RecConfigReadConfigPath(nullptr, ts::filename::PLUGIN_YAML);
  if (access(yaml_path, R_OK) != 0) {
    return plugin_init(validateOnly);
  }

  Note("%s loading ...", ts::filename::PLUGIN_YAML);

  auto result = parse_plugin_yaml(yaml_path.get());
  if (!result.ok()) {
    Error("%s: %s", ts::filename::PLUGIN_YAML, std::string(result.errata.front().text()).c_str());
    return false;
  }

  s_plugin_load_summary.source = ts::filename::PLUGIN_YAML;
  s_plugin_load_summary.entries.clear();

  bool retVal   = true;
  int  index    = 0;
  int  loaded   = 0;
  int  disabled = 0;

  for (const auto &entry : result.value) {
    ++index;

    if (!entry.enabled) {
      Note("plugin #%d skipped: %s (enabled: false)", index, entry.path.c_str());
      s_plugin_load_summary.entries.push_back({entry.path, entry.load_order, false, false, index});
      ++disabled;
      continue;
    }

    auto args = build_plugin_args(entry);
    if (!args) {
      return false;
    }

    std::vector<char *> argv_ptrs;
    std::vector<char *> expanded;

    for (auto &a : *args) {
      char *var = plugin_expand(a.data());
      expanded.emplace_back(var);
      argv_ptrs.emplace_back(var ? var : a.data());
    }
    argv_ptrs.emplace_back(nullptr);

    if (entry.load_order >= 0) {
      Note("plugin #%d loading: %s (load_order: %d)", index, entry.path.c_str(), entry.load_order);
    } else {
      Note("plugin #%d loading: %s", index, entry.path.c_str());
    }

    retVal = single_plugin_init(static_cast<int>(args->size()), argv_ptrs.data(), validateOnly);
    s_plugin_load_summary.entries.push_back({entry.path, entry.load_order, true, retVal, index});
    ++loaded;

    for (auto *v : expanded) {
      ats_free(v);
    }

    if (!retVal) {
      break;
    }
  }

  if (retVal) {
    log_plugin_load_summary(loaded, disabled);
  } else {
    Error("%s failed to load", ts::filename::PLUGIN_YAML);
  }
  return retVal;
}
