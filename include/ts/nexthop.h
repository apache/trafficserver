/** @file

  Traffic Server SDK API header file

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

  @section developers Developers

  NextHop plugin interface.

 */

#pragma once

#include <ts/apidefs.h>

// plugin callback commands.
enum NHCmd { NH_MARK_UP, NH_MARK_DOWN };

struct NHHealthStatus {
  virtual bool isNextHopAvailable(TSHttpTxn txn, const char *hostname, const int port, void *ih = nullptr) = 0;
  virtual void markNextHop(TSHttpTxn txn, const char *hostname, const int port, const NHCmd status, void *ih = nullptr,
                           const time_t now = 0)                                                           = 0;
  virtual ~NHHealthStatus() {}
};

struct NHPluginStrategy {
  virtual void findNextHop(TSHttpTxn txnp, void *ih = nullptr)   = 0;
  virtual bool nextHopExists(TSHttpTxn txnp, void *ih = nullptr) = 0;
  virtual ~NHPluginStrategy() {}

  NHHealthStatus *healthStatus;
};
