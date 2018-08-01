/** @file
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

#include "Data.h"

#include "ts/ts.h"

/** Functions to deal with the connection to the client.
 * Body content transfers are handled by the client.
 * New block requests are also initiated by the client.
 */

/** returns true if the incoming vio can be turned off
 */
bool handle_client_req(TSCont contp, TSEvent event, Data *const data);

void handle_client_resp(TSCont contp, TSEvent event, Data *const data);
