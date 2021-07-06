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

#include "tscore/Diags.h"
#include "tscore/BaseLogFile.h"

struct DiagsConfig {
  void reconfigure_diags();
  void config_diags_norecords();
  void parse_output_string(char *s, DiagsModeOutput *o);
  void register_diags_callbacks();

  /** DiagsConfig constructor.
   *
   * As a side effect, this sets the global diags pointer to newly
   * constructed _diags value.
   */
  DiagsConfig(std::string_view prefix_string, const char *filename, const char *tags, const char *actions, bool use_records = true);
  ~DiagsConfig();

private:
  bool callbacks_established;
  BaseLogFile *diags_log;
  Diags *_diags;
};
