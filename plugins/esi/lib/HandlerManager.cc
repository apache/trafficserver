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

#include <dlfcn.h>

#include "HandlerManager.h"

using std::map;
using std::string;
using namespace EsiLib;

#define CLASS_NAME "HandlerManager"

const char *const HandlerManager::FACTORY_FUNCTION_NAME = "createSpecialIncludeHandler";

void
HandlerManager::loadObjects(const Utils::KeyValueMap &handlers)
{
  ModuleHandleMap::iterator path_map_iter;

  for (Utils::KeyValueMap::const_iterator id_map_iter = handlers.begin(); id_map_iter != handlers.end(); ++id_map_iter) {
    const string &id   = id_map_iter->first;
    const string &path = id_map_iter->second;

    path_map_iter = _path_to_module_map.find(path);

    if (path_map_iter != _path_to_module_map.end()) {
      // we have already loaded this object; just point id to original handle
      _id_to_function_map.insert(FunctionHandleMap::value_type(id, (path_map_iter->second).function));
    } else {
      // no, we have to load this object
      void *obj_handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
      if (!obj_handle) {
        _errorLog("[%s::%s] Could not load module [%s]. Error [%s]", CLASS_NAME, __FUNCTION__, path.c_str(), dlerror());
      } else {
        SpecialIncludeHandlerCreator func_handle = (SpecialIncludeHandlerCreator)(dlsym(obj_handle, FACTORY_FUNCTION_NAME));
        if (!func_handle) {
          _errorLog("[%s::%s] Could not find factory function [%s] in module [%s]. Error [%s]", CLASS_NAME, __FUNCTION__,
                    FACTORY_FUNCTION_NAME, path.c_str(), dlerror());
          dlclose(obj_handle);
        } else {
          _id_to_function_map.insert(FunctionHandleMap::value_type(id, func_handle));
          _path_to_module_map.insert(ModuleHandleMap::value_type(path, ModuleHandles(obj_handle, func_handle)));
          _debugLog(_debug_tag, "[%s] Loaded handler module [%s]", __FUNCTION__, path.c_str());
        }
      }
    }
  }
}

SpecialIncludeHandler *
HandlerManager::getHandler(Variables &esi_vars, Expression &esi_expr, HttpDataFetcher &fetcher, const std::string &id) const
{
  FunctionHandleMap::const_iterator iter = _id_to_function_map.find(id);
  if (iter == _id_to_function_map.end()) {
    _errorLog("[%s::%s] handler id [%s] does not map to any loaded object", CLASS_NAME, __FUNCTION__, id.c_str());
    return 0;
  }
  return (*(iter->second))(esi_vars, esi_expr, fetcher, id);
}

HandlerManager::~HandlerManager()
{
  for (ModuleHandleMap::iterator map_iter = _path_to_module_map.begin(); map_iter != _path_to_module_map.end(); ++map_iter) {
    dlclose((map_iter->second).object);
  }
}
