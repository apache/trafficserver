/** @file Support for common diagnostics between core, plugins, and libswoc.

  This enables specifying the set of methods usable by a user agent based on the remove IP address
  for a user agent connection.

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

#include "tsutil/ts_diag_levels.h"
#include "tsutil/ts_errata.h"

void
Initialize_Errata_Settings()
{
  swoc::Errata::DEFAULT_SEVERITY = ERRATA_ERROR;
  swoc::Errata::FAILURE_SEVERITY = ERRATA_WARN;
  swoc::Errata::SEVERITY_NAMES   = swoc::MemSpan<swoc::TextView const>(Severity_Names.data(), Severity_Names.size());
}
