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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

int         reserve_user_arg(PluginConfig &config);
void        set_user_arg(void *container, PluginConfig &config, JAxContext *ctx);
JAxContext *get_user_arg(void *container, PluginConfig &config);
void        refresh_user_arg(void *container, PluginConfig &config);
void        cleanup_user_arg(void *container, PluginConfig &config);

/* Session copies of fingerprints for the --log-field callback. ATS can close the client vconn, and thereby
 * drop its context, before it logs a transaction, and it runs TXN_CLOSE hooks, which drop transaction
 * contexts, before logging too. The session lives until its transactions are logged.
 *
 * The copies are private to this plugin: they live in a session slot reserved under a fixed name, not the
 * --export registry, and hold only what the log field needs. They are not kept on HTTP/3 sessions, which
 * never fire SSN_CLOSE to free them.
 */
int reserve_session_user_arg(PluginConfig &config);

/** Save a copy of a fingerprint on a session, if any --log-field instance is loaded.
 *
 * @param[in] ssnp The session.
 * @param[in] config The configuration of the instance whose context holds @a fingerprint.
 * @param[in] fingerprint The fingerprint.
 * @param[in] txn_id The transaction the fingerprint belongs to, or std::nullopt for a connection fingerprint.
 */
void save_session_fingerprint(TSHttpSsn ssnp, PluginConfig &config, std::string_view fingerprint,
                              std::optional<uint64_t> txn_id = std::nullopt);

/** Look up the fingerprint saved on a session.
 *
 * @param[in] ssnp The session.
 * @param[in] config The plugin configuration.
 * @param[in] txn_id The transaction that must have saved the copy, or std::nullopt for a connection fingerprint.
 * @return The fingerprint, or nullptr if none was saved for @a txn_id.
 */
std::string const *get_session_fingerprint(TSHttpSsn ssnp, PluginConfig &config, std::optional<uint64_t> txn_id = std::nullopt);

/** Free the copies of all instances saved on a session. */
void cleanup_session_user_arg(TSHttpSsn ssnp);
