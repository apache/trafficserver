/** @file

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

#include "context.h"
#include "config.h"

#include "ts/ts.h"

int         reserve_user_arg(PluginConfig &config);
void        set_user_arg(void *container, PluginConfig &config, JAxContext *ctx);
JAxContext *get_user_arg(void *container, PluginConfig &config);
void        refresh_user_arg(void *container, PluginConfig &config);
void        cleanup_user_arg(void *container, PluginConfig &config);

/* Session copies of fingerprints for the --log-field callback. ATS can close the client vconn, and thereby
 * drop its context, before it logs a transaction, and it runs TXN_CLOSE hooks, which drop transaction
 * contexts, before logging too. The session lives until its transactions are logged.
 */
int         reserve_session_user_arg(PluginConfig &config);
void        copy_to_session_user_arg(TSHttpSsn ssnp, PluginConfig &config, JAxContext const &ctx, uint64_t txn_id = 0);
JAxContext *get_session_user_arg(TSHttpSsn ssnp, PluginConfig &config);
void        cleanup_session_user_arg(TSHttpSsn ssnp, PluginConfig &config);
