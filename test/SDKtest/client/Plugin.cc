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

/*************************** -*- Mod: C++ -*- *********************

 Plugin.cc
******************************************************************/
#include "Plugin.h"

#if defined(hpux)
#include <dl.h>

void *
dlsym(void *handle, const char *name)
{
  void *p;

  shl_findsym((shl_t *) & handle, name, TYPE_UNDEFINED, &p);
  return p;
}

void *
dlopen(char *name, unsigned int flag)
{
  return shl_load(name, BIND_IMMEDIATE, 0);     /* hack, we know RTLD_NOW */
}

int
dlclose(void *handle)
{
  return shl_unload((shl_t) handle);
}

char *
dlerror(void)
{
  return strerror(errno);
}
#endif

INKPlugin::INKPlugin(int cid, char *api)
{
  client_id = cid;
  handle = NULL;
  path = api;

  plugin_init_fcn = NULL;
  options_process_fcn = NULL;
  options_process_finish_fcn = NULL;
  plugin_finish_fcn = NULL;
  connection_finish_fcn = NULL;
  request_create_fcn = NULL;
  header_process_fcn = NULL;
  partial_body_process_fcn = NULL;
  report_fcn = NULL;
}


void
INKPlugin::load_plugin()
{
  if (strcmp(path, "")) {
    char *plugin_path = (char *) malloc(strlen(path) + 3);
    sprintf(plugin_path, "./%s", path);

    fprintf(stderr, "\nClient %d loading plugin %s ...\n", client_id, plugin_path + 2);

    handle = dlopen(plugin_path, RTLD_NOW);

    if (!handle) {
      fprintf(stderr, "unable to load client plugin\n");
      perror("client plugin");
      exit(1);
    }
    plugin_init_fcn = (PluginInit) dlsym(handle, "INKPluginInit");

    if (!plugin_init_fcn) {
      fprintf(stderr, "unable to find INKPluginInit function: %s", dlerror());
      dlclose(handle);
      exit(1);
    }
    plugin_init_fcn(client_id);
    fprintf(stderr, "Client %d finish loading plugin\n\n", client_id);
    free(plugin_path);
  }
}

extern "C"
{
  void INKFuncRegister(INKPluginFuncId fid)
  {
    plug_in->register_funct(fid);
  }
}

void
INKPlugin::register_funct(INKPluginFuncId fid)
{
  switch (fid) {
  case INK_FID_OPTIONS_PROCESS:
    options_process_fcn = (OptionsProcess) dlsym(handle, "INKOptionsProcess");
    break;

  case INK_FID_OPTIONS_PROCESS_FINISH:
    options_process_finish_fcn = (OptionsProcessFinish) dlsym(handle, "INKOptionsProcessFinish");
    break;

  case INK_FID_CONNECTION_FINISH:
    connection_finish_fcn = (ConnectionFinish) dlsym(handle, "INKConnectionFinish");
    break;

  case INK_FID_PLUGIN_FINISH:
    plugin_finish_fcn = (PluginFinish) dlsym(handle, "INKPluginFinish");
    break;

  case INK_FID_REQUEST_CREATE:
    request_create_fcn = (RequestCreate) dlsym(handle, "INKRequestCreate");
    break;

  case INK_FID_HEADER_PROCESS:
    header_process_fcn = (HeaderProcess) dlsym(handle, "INKHeaderProcess");
    break;

  case INK_FID_PARTIAL_BODY_PROCESS:
    partial_body_process_fcn = (PartialBodyProcess) dlsym(handle, "INKPartialBodyProcess");
    break;

  case INK_FID_REPORT:
    report_fcn = (Report) dlsym(handle, "INKReport");
    break;

  default:
    fprintf(stderr, "Can't register function: unknown type of INKPluginFuncId");
    break;
  }
}
