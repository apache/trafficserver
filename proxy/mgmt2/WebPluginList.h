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

#ifndef _WEB_PLUGIN_LIST_H_
#define _WEB_PLUGIN_LIST_H_

/****************************************************************************
 *
 *  WebPluginList.h - Keep track of a list of web configurable plugins
 *
 *
 *
 ****************************************************************************/

#include "ink_platform.h"
#include "List.h"

struct WebPluginConfig
{
  WebPluginConfig(const char *aName, const char *aConfigPath)
  {
    name = xstrdup(aName);
    config_path = xstrdup(aConfigPath);
    next = NULL;
  }
   ~WebPluginConfig(void)
  {
    free(name);
    free(config_path);
  }

  char *name, *config_path;
  WebPluginConfig *next;
};

class WebPluginList
{
public:
  WebPluginList(void);
   ~WebPluginList(void);

  void clear(void);
  void add(const char *name, const char *config_path);
  WebPluginConfig *getFirst();
  WebPluginConfig *getNext(WebPluginConfig * wpc);

private:
    WebPluginConfig * head, *tail;

};

#endif // !_WEB_PLUGIN_LIST_H_
