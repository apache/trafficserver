/** @file

  A brief file description

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

/****************************************************************************

   MgmtPlugin.cc

   Description: mgmt api plugin init code


 ****************************************************************************/

#include "inktomi++.h"
#include "ink_platform.h"
#include "I_Layout.h"
#include <stdio.h>
#include <list>
//#include "ink_file.h"
//#include "Compatability.h"
#include "ParseRules.h"
#include "Main.h"
#include "MgmtPlugin.h"
//#include "ink_ctype.h"

#include "WebMgmtUtils.h"

#ifndef DIR_SEP
#define DIR_SEP "/"
#endif

#ifndef PATH_NAME_MAX
#define PATH_NAME_MAX 1024
#endif

#ifndef MAX_BUF_SIZE
#define MAX_BUF_SIZE 1024
#endif

// TODO: Make those two char[PATH_NAME_MAX + 1]
//       to avoid strdup
static const char *plugin_dir = NULL;
static const char *config_dir = NULL;

typedef void (*init_func_t) (int argc, char *argv[]);
typedef void (*init_func_w_handle_t) (void *handle, int argc, char *argv[]);

class CleanUpDlopenHandle
{
public:
  void regist(void *handle)
  {
    _handles.push_back(handle);
  }
   ~CleanUpDlopenHandle()
  {
    for (std::list < void *>::const_iterator it = _handles.begin(); it != _handles.end(); ++it) {
      free(*it);
    }
  }
private:
  std::list < void *>_handles;
};

static CleanUpDlopenHandle handles;

//
// dll_open (copied from Plugin.cc)
//
static void *
dll_open(char *fn)
{
  return (void *) dlopen(fn, RTLD_NOW);
}

//
// dll_findsym (copied from Plugin.cc)
//
static void *
dll_findsym(void *dlp, const char *name)
{
  return (void *) dlsym(dlp, name);
}

//
// dll_error (copied from Plugin.cc)
//
static char *
dll_error(void *dlp)
{
  return (char *) dlerror();
}

//
// dll_close (copied from Plugin.cc)
//
static void
dll_close(void *dlp)
{
  dlclose(dlp);
}

//
// mgmt_plugin_load
//
// loads the plugin passing it the argc, argv arguments
//
static void
mgmt_plugin_load(int argc, char *argv[])
{
  char path[PATH_NAME_MAX + 1];
  void *handle;
  init_func_t init;

  if (argc < 1) {
    return;
  }
  if (plugin_dir == NULL)
    plugin_dir = Layout::get()->sysconfdir;
  ink_filepath_make(path, sizeof(path), plugin_dir, argv[0]);

  Debug("plugin", "[mgmt_plugin_load] loading plugin: '%s'", path);

  handle = dll_open(path);
  if (!handle) {
    Warning("[mgmt_plugin_load] unable to load '%s': %s", path, dll_error(handle));
    return;
  }
  // register handle for future resource release
  handles.regist(handle);

  // coverity[noescape]
  init_func_w_handle_t inith = (init_func_w_handle_t) dll_findsym(handle, "INKPluginInitwDLLHandle");
  if (inith) {
    inith(handle, argc, argv);
    return;
  }

  init = (init_func_t) dll_findsym(handle, "INKPluginInit");
  if (!init) {
    Warning("[mgmt_plugin_load] unable to find INKPluginInit function '%s': %s", path, dll_error(handle));
    dll_close(handle);
    return;
  }
  // coverity[noescape]
  // coverity[leaked_storage]
  init(argc, argv);             // call plug-in's INKPluginInit
}

//
// mgmt_plugin_expand
//
// expands any variables (if any) for a plugin's argument list in
// plugin_mgmt.config
//
static char *
mgmt_plugin_expand(char *arg)
{
  char str[MAX_BUF_SIZE];
  memset(str, 0, MAX_BUF_SIZE);

  if (*arg == '$') {
    arg += 1;
    varStrFromName(arg, str, MAX_BUF_SIZE);
  }

  if (str[0] == '\0')
    return NULL;
  else
    return (xstrdup(str));
}

//
// plugin_init
//
// Reads in the plugin_mgmt.config file and loads each plugin listed
// Gets passed in the root directory; ASSUMES that the root directory is where
// everything is installed!!
//
void
mgmt_plugin_init(const char *config_path)
{
  char temp_dir[MAX_BUF_SIZE];
  char path[PATH_NAME_MAX + 1];
  char line[1024], *p;
  char *argv[64];
  char *vars[64];
  int argc;
  int fd;
  int i;

  Debug("plugin", "[mgmt_plugin_init] START\n");

  //api_init ();

  // get the directory where plugins are stored from records.config
  // can be absolute or relative path
  memset(temp_dir, 0, MAX_BUF_SIZE);
  varStrFromName("proxy.config.plugin.plugin_mgmt_dir", temp_dir, MAX_BUF_SIZE);

  Debug("plugin", "[mgmt_plugin_init] proxy.config.plugin.pugin_mgmt_dir = %s", temp_dir);
  if (temp_dir[0] == '\0') {    // ERROR: can't find record
    Warning("[mgmt_plugin_init] unable to get proxy.config.plugin.plugin_mgmt_dir record value");
    return;
  } else {
    plugin_dir = Layout::get()->relative(temp_dir);
  }

  if (config_path)
    config_dir = xstrdup(config_path);
  else
    config_dir = Layout::get()->sysconfdir;
  ink_filepath_make(path, sizeof(path), config_dir, "plugin_mgmt.config");

  // open plugin_mgmt.config
  fd = open(path, O_RDONLY);
  if (fd < 0) {                 // ERROR: can't open file
    Warning("[mgmt_plugin_init] unable to open plugin config file '%s': %d, %s", path, errno, strerror(errno));
    return;
  }
  // read each line of mgmt_plugin.config; parse the plugin name and args
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
      vars[i] = mgmt_plugin_expand(argv[i]);
      if (vars[i]) {
        argv[i] = vars[i];
      }
    }

    mgmt_plugin_load(argc, argv);

    for (i = 0; i < argc; i++) {
      if (vars[i]) {
        xfree(vars[i]);
      }
    }
  }

  close(fd);
}
