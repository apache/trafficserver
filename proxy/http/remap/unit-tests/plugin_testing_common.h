/** @file

  A test plugin header for testing Plugin's Dynamic Shared Objects (DSO)

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

  @section details Details

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#pragma once

#include <map>
#include <string>
#include <iostream>

#include <stdio.h>
#include <stdarg.h>

#include "../PluginFactory.h"

extern thread_local PluginThreadContext *pluginThreadContext;

/* A temp sandbox to play with our toys used for all fun with this test-bench */
fs::path getTemporaryDir();

class PluginDebugObject
{
public:
  PluginDebugObject() { clear(); }

  void
  clear()
  {
    contextInit            = nullptr;
    contextInitInstance    = nullptr;
    doRemapCalled          = 0;
    initCalled             = 0;
    doneCalled             = 0;
    initInstanceCalled     = 0;
    deleteInstanceCalled   = 0;
    preReloadConfigCalled  = 0;
    postReloadConfigCalled = 0;
    postReloadConfigStatus = TSREMAP_CONFIG_RELOAD_FAILURE;
    ih                     = nullptr;
    argc                   = 0;
    argv                   = nullptr;
  }

  /* Input fields used to set the test behavior of the plugin call-backs */
  bool fail = false; /* tell the plugin call-back to fail for testing purposuses */
  void *input_ih;    /* the value to be returned by the plugin instance init function */

  /* Output fields showing what happend during the test */
  const PluginThreadContext *contextInit         = nullptr;                   /* plugin initialization context */
  const PluginThreadContext *contextInitInstance = nullptr;                   /* plugin instance initialization context */
  int doRemapCalled                              = 0;                         /* mark if remap was called */
  int initCalled                                 = 0;                         /* mark if plugin init was called */
  int doneCalled                                 = 0;                         /* mark if done was called */
  int initInstanceCalled                         = 0;                         /* mark if instance init was called */
  int deleteInstanceCalled                       = 0;                         /* mark if delete instance was called */
  int preReloadConfigCalled                      = 0;                         /* mark if pre-reload config was called */
  int postReloadConfigCalled                     = 0;                         /* mark if post-reload config was called */
  TSRemapReloadStatus postReloadConfigStatus = TSREMAP_CONFIG_RELOAD_FAILURE; /* mark if plugin reload status is passed correctly */
  void *ih                                   = nullptr;                       /* instance handler */
  int argc                                   = 0;       /* number of plugin instance parameters received by the plugin */
  char **argv                                = nullptr; /* plugin instance parameters received by the plugin */
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef void *GetPluginDebugObjectFunction(void);
GetPluginDebugObjectFunction getPluginDebugObjectTest;

#define PluginDebug(category, fmt, ...) \
  PrintToStdErr("(%s) %s:%d:%s() " fmt "\n", category, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define PluginError(fmt, ...) PrintToStdErr("%s:%d:%s() " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
void PrintToStdErr(const char *fmt, ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

// functions to support unit-testing of option to enable/disable dynamic reload of plugins
void enablePluginDynamicReload();
void disablePluginDynamicReload();