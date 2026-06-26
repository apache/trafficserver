/** @file

  traffic_ctl command for inspecting and clearing the cache shared-memory
  control segment and its associated stripe segments.

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

#include "CtrlCommands.h"

#include <string>

class CacheShmCommand : public CtrlCommand
{
public:
  CacheShmCommand(ts::Arguments *args);

private:
  static inline const std::string STATUS_STR{"status"};
  static inline const std::string CLEAR_STR{"clear"};
  static inline const std::string PREFIX_STR{"prefix"};

  void status();
  void clear();

  std::string get_prefix();
};
