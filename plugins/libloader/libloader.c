/*****************************************************************************
 * Copyright (C) 2011-13 Qualys, Inc
 * Copyright (C) 2013 The Apache Software Foundation
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * QUALYS licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************/

/* libloader: load libraries, making all symbols exported
 * universally visible.  Equivalent to LoadFile in HTTPD.
 *
 * Written for ironbee plugin, whose module architecture
 * is not compatible with trafficserver's plugins.
 * May be useful for other plugins with non-trivial
 * library dependencies.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <ts/ts.h>

typedef struct {
  void *handle;
  void *next;
} link_handle;

static link_handle *libs = NULL;

static void
unloadlibs(void)
{
  link_handle *p = libs;
  while (p != NULL) {
    link_handle *next = p->next;
    dlclose(p->handle);
    TSfree(p);
    p = next;
  }
  libs = NULL;
}

void
TSPluginInit(int argc, const char *argv[])
{
  int i;
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"libloader";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[libloader] Plugin registration failed.\n");
    return;
  }

  atexit(unloadlibs);

  for (i = 1; i < argc; ++i) {
    const char *lib = argv[i];
    void *handle    = dlopen(lib, RTLD_GLOBAL | RTLD_NOW);
    if (handle) {
      link_handle *l = TSmalloc(sizeof(link_handle));
      l->handle      = handle;
      l->next        = libs;
      libs           = l;
      TSDebug("libloader", " loaded %s\n", lib);
    } else {
      TSError("[libloader] failed to load %s: %s\n", lib, dlerror());
    }
  }
  return;
}
