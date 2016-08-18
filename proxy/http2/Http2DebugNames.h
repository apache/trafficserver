/** @file

  Http2DebugNames

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

#ifndef __HTTP2_DEBUG_NAMES_H__
#define __HTTP2_DEBUG_NAMES_H__

#include "ts/ink_defs.h"

class Http2DebugNames
{
public:
  static const char *get_settings_param_name(uint16_t id);
  static const char *get_state_name(uint16_t id);
};

#endif // __HTTP2_DEBUG_NAMES_H__
