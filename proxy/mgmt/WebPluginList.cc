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

#include "libts.h"

#include "WebPluginList.h"

/****************************************************************************
 *
 *  WebPluginList.cc - Keep track of a list of web configurable plugins
 *
 *
 *
 ****************************************************************************/

WebPluginList::WebPluginList(void)
{
  head = tail = NULL;
}

WebPluginList::~WebPluginList(void)
{
  WebPluginConfig *cur = head, *save;
  while (cur != NULL) {
    save = cur;
    cur = cur->next;
    delete save;
  }
}

void
WebPluginList::clear(void)
{
  WebPluginConfig *cur = head, *save;
  while (cur != NULL) {
    save = cur;
    cur = cur->next;
    delete save;
  }
  head = tail = NULL;
}

void
WebPluginList::add(const char *name, const char *config_path)
{
  WebPluginConfig *new_plugin = new WebPluginConfig(name, config_path);
  if (head == NULL) {
    head = new_plugin;
  } else {
    tail->next = new_plugin;
  }
  tail = new_plugin;
}

WebPluginConfig *
WebPluginList::getFirst()
{
  return head;
}

WebPluginConfig *
WebPluginList::getNext(WebPluginConfig * wpc)
{
  if (wpc) {
    return wpc->next;
  }
  return NULL;
}
