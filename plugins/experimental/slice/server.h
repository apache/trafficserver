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

/** Functions to handle the connection to the server.
 * In particular slice block header responses are handled here.
 * Data transfers are handled by the client code which pulls
 * the data from the server side.
 *
 * Special case is when the client connection has been closed
 * because of client data request being fulfilled or
 * when the client aborts.  The current slice block will
 * continue reading to ensure the whole block is transferred
 * to cache.
 */

void handle_server_resp(TSCont contp, TSEvent event, Data *const data);
