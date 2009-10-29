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

 Plugin.h
******************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <ctype.h>
#include "Defines.h"
#include "api/ClientAPI.h"

#ifndef _Plugin_h_
#define _Plugin_h_

typedef void (*PluginInit) (int);
typedef void (*OptionsProcess) (char *, char *);
typedef void (*OptionsProcessFinish) ();
typedef void (*ConnectionFinish) (void *, INKConnectionStatus);
typedef void (*PluginFinish) ();
typedef int (*RequestCreate) (char *, int, char *, int, char *, int, void **);
typedef INKRequestAction(*HeaderProcess) (void *, char *, int, char *);
typedef INKRequestAction(*PartialBodyProcess) (void *, void *, int, int);
typedef void (*Report) ();

class INKPlugin
{
public:
  int client_id;
  void *handle;
  char *path;

  PluginInit plugin_init_fcn;
  OptionsProcess options_process_fcn;
  OptionsProcessFinish options_process_finish_fcn;
  PluginFinish plugin_finish_fcn;
  ConnectionFinish connection_finish_fcn;
  RequestCreate request_create_fcn;
  HeaderProcess header_process_fcn;
  PartialBodyProcess partial_body_process_fcn;
  Report report_fcn;

    INKPlugin(int cid, char *api);
  void load_plugin();
  void register_funct(INKPluginFuncId fid);
};

extern INKPlugin *plug_in;

#endif // _Plugin_h_
