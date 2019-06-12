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

#include <cstdio>
#include <string>

namespace EsiLib
{
#define DEBUG_TAG_MAX_SIZE 64

/** class that has common private characteristics */
class ComponentBase
{
public:
  typedef void (*Debug)(const char *, const char *, ...);
  typedef void (*Error)(const char *, ...);

protected:
  ComponentBase(const char *debug_tag, Debug debug_func, Error error_func) : _debugLog(debug_func), _errorLog(error_func)
  {
    snprintf(_debug_tag, sizeof(_debug_tag), "%s", debug_tag);
  };

  char _debug_tag[DEBUG_TAG_MAX_SIZE];
  Debug _debugLog;
  Error _errorLog;

  virtual ~ComponentBase(){};
};
}; // namespace EsiLib
