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

/*****************************************************************************
 *
 *  ControlBase.h - Base class to process generic modifiers to
 *                         ControlMatcher Directives
 *
 *
 ****************************************************************************/


#ifndef _CONTROL_BASE_H_
#define _CONTROL_BASE_H_

#include "libts.h"
#include "DynArray.h"
class HttpRequestData;
class Tokenizer;
struct matcher_line;

// MOD_IPORT added
enum ModifierTypes
{ MOD_INVALID, MOD_PORT, MOD_SCHEME, MOD_PREFIX,
  MOD_SUFFIX, MOD_METHOD, MOD_TIME, MOD_SRC_IP,
  MOD_IPORT, MOD_TAG
};

struct modifier_el
{
  ModifierTypes type;
  void *opaque_data;
};

class ControlBase
{
public:
  ControlBase();
  ~ControlBase();
  const char *ProcessModifiers(matcher_line * line_info);
  bool CheckModifiers(HttpRequestData * request_data);
  bool CheckForMatch(HttpRequestData * request_data, int last_number);
  void Print();
  int line_num;
  const void *getModElem(ModifierTypes t);
private:
    DynArray<modifier_el> *mod_elements;
  const char *ProcessSrcIp(char *val, void **opaque_ptr);
  const char *ProcessTimeOfDay(char *val, void **opaque_ptr);
  const char *ProcessPort(char *val, void **opaque_ptr);
};

inline
ControlBase::ControlBase():
line_num(0),
mod_elements(NULL)
{
}

inline bool
ControlBase::CheckForMatch(HttpRequestData * request_data, int last_number)
{
  if ((last_number<0 || last_number> this->line_num) && this->CheckModifiers(request_data)) {
    return true;
  } else {
    return false;
  }
}

#endif
