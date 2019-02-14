/** @file

  This file used for catch based tests. It is the main() stub.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include <vector>
#include <string>
#include "tscore/BufferWriter.h"
#include "tscore/ink_resolver.h"
#include "test_Diags.h"

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

Diags *diags = new CatchDiags;
extern void ts_session_protocol_well_known_name_indices_init();

int
main(int argc, char *argv[])
{
  // Global data initialization needed for the unit tests.
  ts_session_protocol_well_known_name_indices_init();
  // Cheat for ts_host_res_global_init as there's no records.config to check for non-default.
  memcpy(host_res_default_preference_order, HOST_RES_DEFAULT_PREFERENCE_ORDER, sizeof(host_res_default_preference_order));

  int result = Catch::Session().run(argc, argv);

  // global clean-up...

  return result;
}
