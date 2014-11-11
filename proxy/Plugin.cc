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

#include <stdio.h>
#include "ink_platform.h"
#include "ink_file.h"
#include "Compatability.h"
#include "ParseRules.h"
#include "I_RecCore.h"
#include "I_Layout.h"
#include "InkAPIInternal.h"
#include "Main.h"
#include "Plugin.h"
#include "ink_cap.h"

static const char *plugin_dir = ".";

typedef void (*init_func_t) (int argc, char *argv[]);

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
PluginRegInfo *plugin_reg_current = NULL;

PluginRegInfo::PluginRegInfo()
  : plugin_registered(false), plugin_path(NULL), sdk_version(PLUGIN_SDK_VERSION_UNKNOWN),
    plugin_name(NULL), vendor_name(NULL), support_email(NULL)
{ }

static void
plugin_load(int argc, char *argv[])
{
  char path[PATH_NAME_MAX + 1];
  void *handle;
  init_func_t init;
  PluginRegInfo *plugin_reg_temp;

  if (argc < 1) {
    return;
  }
  ink_filepath_make(path, sizeof(path), plugin_dir, argv[0]);

  Note("loading plugin '%s'", path);

  plugin_reg_temp = plugin_reg_list.head;
  while (plugin_reg_temp) {
    if (strcmp(plugin_reg_temp->plugin_path, path) == 0) {
      Warning("multiple loading of plugin %s", path);
      break;
    }
    plugin_reg_temp = (plugin_reg_temp->link).next;
  }
  // elevate the access to read files as root if compiled with capabilities, if not
  // change the effective user to root
  {

#if TS_USE_POSIX_CAP
    uint32_t elevate_access = 0;
    REC_ReadConfigInteger(elevate_access, "proxy.config.plugin.load_elevated");
    ElevateAccess access(elevate_access != 0);
#endif /* TS_USE_POSIX_CAP */

    handle = dlopen(path, RTLD_NOW);
    if (!handle) {
      Fatal("unable to load '%s': %s", path, dlerror());
    }

    // Allocate a new registration structure for the
    //    plugin we're starting up
    ink_assert(plugin_reg_current == NULL);
    plugin_reg_current = new PluginRegInfo;
    plugin_reg_current->plugin_path = ats_strdup(path);

    init = (init_func_t) dlsym(handle, "TSPluginInit");
    if (!init) {
      Fatal("unable to find TSPluginInit function in '%s': %s", path, dlerror());
      return; // this line won't get called since Fatal brings down ATS
    }

    init(argc, argv);
  } // done elevating access

  plugin_reg_list.push(plugin_reg_current);
  plugin_reg_current = NULL;
}

static char *
plugin_expand(char *arg)
{
  RecDataT data_type;
  char *str = NULL;

  if (*arg != '$') {
    return (char *) NULL;
  }
  // skip the $ character
  arg += 1;

  if (RecGetRecordDataType(arg, &data_type) != REC_ERR_OKAY) {
    goto not_found;
  }

  switch (data_type) {
  case RECD_STRING:
    {
      RecString str_val;
      if (RecGetRecordString_Xmalloc(arg, &str_val) != REC_ERR_OKAY) {
        goto not_found;
      }
      return (char *) str_val;
      break;
    }
  case RECD_FLOAT:
    {
      RecFloat float_val;
      if (RecGetRecordFloat(arg, &float_val) != REC_ERR_OKAY) {
        goto not_found;
      }
      str = (char *)ats_malloc(128);
      snprintf(str, 128, "%f", (float) float_val);
      return str;
      break;
    }
  case RECD_INT:
    {
      RecInt int_val;
      if (RecGetRecordInt(arg, &int_val) != REC_ERR_OKAY) {
        goto not_found;
      }
      str = (char *)ats_malloc(128);
      snprintf(str, 128, "%ld", (long int) int_val);
      return str;
      break;
    }
  case RECD_COUNTER:
    {
      RecCounter count_val;
      if (RecGetRecordCounter(arg, &count_val) != REC_ERR_OKAY) {
        goto not_found;
      }
      str = (char *)ats_malloc(128);
      snprintf(str, 128, "%ld", (long int) count_val);
      return str;
      break;
    }
  default:
    goto not_found;
    break;
  }


not_found:
  Warning("plugin.config: unable to find parameter %s", arg);
  return NULL;
}

void
plugin_init(void)
{
  ats_scoped_str path;
  char line[1024], *p;
  char *argv[64];
  char *vars[64];
  int argc;
  int fd;
  int i;
  static bool INIT_ONCE = true;

  if (INIT_ONCE) {
    api_init();
    TSConfigDirGet();
    plugin_dir = TSPluginDirGet();
    INIT_ONCE = false;
  }

  path = RecConfigReadConfigPath(NULL, "plugin.config");
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    Warning("unable to open plugin config file '%s': %d, %s", (const char *)path, errno, strerror(errno));
    return;
  }

  while (ink_file_fd_readline(fd, sizeof(line) - 1, line) > 0) {
    argc = 0;
    p = line;

    // strip leading white space and test for comment or blank line
    while (*p && ParseRules::is_wslfcr(*p))
      ++p;
    if ((*p == '\0') || (*p == '#'))
      continue;

    // not comment or blank, so rip line into tokens
    while (1) {
      while (*p && ParseRules::is_wslfcr(*p))
        ++p;
      if ((*p == '\0') || (*p == '#'))
        break;                  // EOL

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

    plugin_load(argc, argv);

    for (i = 0; i < argc; i++)
      ats_free(vars[i]);
  }

  close(fd);
}

