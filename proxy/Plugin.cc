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
#include "tscore/ink_platform.h"
#include "tscore/ink_file.h"
#include "tscore/ParseRules.h"
#include "records/I_RecCore.h"
#include "tscore/I_Layout.h"
#include "InkAPIInternal.h"
#include "Plugin.h"
#include "tscore/ink_cap.h"
#include "tscore/Filenames.h"

#define MAX_PLUGIN_ARGS 64

static PluginDynamicReloadMode plugin_dynamic_reload_mode = PluginDynamicReloadMode::RELOAD_ON;

bool
isPluginDynamicReloadEnabled()
{
  return PluginDynamicReloadMode::RELOAD_ON == plugin_dynamic_reload_mode;
}

void
parsePluginDynamicReloadConfig()
{
  int int_plugin_dynamic_reload_mode;

  REC_ReadConfigInteger(int_plugin_dynamic_reload_mode, "proxy.config.plugin.dynamic_reload_mode");
  plugin_dynamic_reload_mode = static_cast<PluginDynamicReloadMode>(int_plugin_dynamic_reload_mode);

  if (plugin_dynamic_reload_mode < 0 || plugin_dynamic_reload_mode >= PluginDynamicReloadMode::RELOAD_COUNT) {
    Warning("proxy.config.plugin.dynamic_reload_mode out of range. using default value.");
    plugin_dynamic_reload_mode = PluginDynamicReloadMode::RELOAD_ON;
  }
  Note("Initialized plugin_dynamic_reload_mode: %d", plugin_dynamic_reload_mode);
}

void
parsePluginConfig()
{
  parsePluginDynamicReloadConfig();
}

static const char *plugin_dir = ".";

using init_func_t = void (*)(int, char **);

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
PluginRegInfo *plugin_reg_current = nullptr;

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
    return false;
  }

  return true;
}

static bool
single_plugin_init(int argc, char *argv[], bool validateOnly)
{
  char path[PATH_NAME_MAX];
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
    REC_ReadConfigInteger(elevate_access, "proxy.config.plugin.load_elevated");
    ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

    void *handle, *initptr = nullptr;
    std::string error;
    bool loaded = plugin_dso_load(path, handle, initptr, error);
    init        = reinterpret_cast<init_func_t>(initptr);

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
    Fatal("plugin not registered by calling TSPluginRegister");
    return false; // this line won't get called since Fatal brings down ATS
  }

  plugin_reg_current = nullptr;

  return true;
}

static char *
plugin_expand(char *arg)
{
  RecDataT data_type;
  char *str = nullptr;

  if (*arg != '$') {
    return (char *)nullptr;
  }
  // skip the $ character
  arg += 1;

  if (RecGetRecordDataType(arg, &data_type) != REC_ERR_OKAY) {
    goto not_found;
  }

  switch (data_type) {
  case RECD_STRING: {
    RecString str_val;
    if (RecGetRecordString_Xmalloc(arg, &str_val) != REC_ERR_OKAY) {
      goto not_found;
    }
    return static_cast<char *>(str_val);
    break;
  }
  case RECD_FLOAT: {
    RecFloat float_val;
    if (RecGetRecordFloat(arg, &float_val) != REC_ERR_OKAY) {
      goto not_found;
    }
    str = static_cast<char *>(ats_malloc(128));
    snprintf(str, 128, "%f", static_cast<float>(float_val));
    return str;
    break;
  }
  case RECD_INT: {
    RecInt int_val;
    if (RecGetRecordInt(arg, &int_val) != REC_ERR_OKAY) {
      goto not_found;
    }
    str = static_cast<char *>(ats_malloc(128));
    snprintf(str, 128, "%ld", static_cast<long int>(int_val));
    return str;
    break;
  }
  case RECD_COUNTER: {
    RecCounter count_val;
    if (RecGetRecordCounter(arg, &count_val) != REC_ERR_OKAY) {
      goto not_found;
    }
    str = static_cast<char *>(ats_malloc(128));
    snprintf(str, 128, "%ld", static_cast<long int>(count_val));
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
  char line[1024], *p;
  char *argv[MAX_PLUGIN_ARGS];
  char *vars[MAX_PLUGIN_ARGS];
  int argc;
  int fd;
  int i;
  bool retVal           = true;
  static bool INIT_ONCE = true;

  if (INIT_ONCE) {
    api_init();
    plugin_dir = ats_stringdup(RecConfigReadPluginDir());
    INIT_ONCE  = false;
  }

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
    retVal = single_plugin_init(argc, argv, validateOnly);

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
