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

#include "RemapPluginInfo.h"
#include "ts/ink_string.h"
#include "ts/ink_memory.h"

remap_plugin_info::remap_plugin_info(char *_path)
  : next(0),
    path(NULL),
    path_size(0),
    dlh(NULL),
    fp_tsremap_init(NULL),
    fp_tsremap_done(NULL),
    fp_tsremap_new_instance(NULL),
    fp_tsremap_delete_instance(NULL),
    fp_tsremap_do_remap(NULL),
    fp_tsremap_os_response(NULL)
{
  // coverity did not see ats_free
  // coverity[ctor_dtor_leak]
  if (_path && likely((path = ats_strdup(_path)) != NULL))
    path_size = strlen(path);
}

remap_plugin_info::~remap_plugin_info()
{
  ats_free(path);
  if (dlh)
    dlclose(dlh);
}

//
// Find a plugin by path from our linked list
//
remap_plugin_info *
remap_plugin_info::find_by_path(char *_path)
{
  int _path_size        = 0;
  remap_plugin_info *pi = 0;

  if (likely(_path && (_path_size = strlen(_path)) > 0)) {
    for (pi = this; pi; pi = pi->next) {
      if (pi->path && pi->path_size == _path_size && !strcmp(pi->path, _path))
        break;
    }
  }

  return pi;
}

//
// Add a plugin to the linked list
//
void
remap_plugin_info::add_to_list(remap_plugin_info *pi)
{
  remap_plugin_info *p = this;

  if (likely(pi)) {
    while (p->next)
      p = p->next;

    p->next  = pi;
    pi->next = NULL;
  }
}

//
// Remove and delete all plugins from a list, including ourselves.
//
void
remap_plugin_info::delete_my_list()
{
  remap_plugin_info *p = this->next;

  while (p) {
    remap_plugin_info *tmp = p;

    p = p->next;
    delete tmp;
  }

  delete this;
}
