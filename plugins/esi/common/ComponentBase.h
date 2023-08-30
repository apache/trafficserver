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

#include <ts/ts.h>

namespace EsiLib
{
#define DEBUG_TAG_MAX_SIZE 64

/** class that has common private characteristics */
class ComponentBase
{
protected:
  ComponentBase(char const *debug_tag) : _dbg_ctl{debug_tag} {};

  DbgCtl _dbg_ctl;

  virtual ~ComponentBase(){};
};
}; // namespace EsiLib
