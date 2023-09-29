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

#pragma once

#include <string>
#include <map>

#include "Utils.h"
#include "SpecialIncludeHandler.h"
#include "IncludeHandlerFactory.h"
#include "Variables.h"
#include "HttpDataFetcher.h"

namespace EsiLib
{
class HandlerManager
{
public:
  void loadObjects(const Utils::KeyValueMap &handlers);

  SpecialIncludeHandler *getHandler(Variables &esi_vars, Expression &esi_expr, HttpDataFetcher &http_fetcher,
                                    const std::string &id) const;

  ~HandlerManager();

private:
  using FunctionHandleMap = std::map<std::string, SpecialIncludeHandlerCreator>;

  struct ModuleHandles {
    void *object;
    SpecialIncludeHandlerCreator function;
    ModuleHandles(void *o = nullptr, SpecialIncludeHandlerCreator f = nullptr) : object(o), function(f){};
  };

  using ModuleHandleMap = std::map<std::string, ModuleHandles>;

  FunctionHandleMap _id_to_function_map;
  ModuleHandleMap _path_to_module_map;

  static const char *const FACTORY_FUNCTION_NAME;
};
}; // namespace EsiLib
