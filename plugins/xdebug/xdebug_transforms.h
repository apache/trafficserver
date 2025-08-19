/** @file
 *
 * XDebug plugin transforms functionality declarations.
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "ts/ts.h"
#include "xdebug_types.h" // Required for BodyBuilder

namespace xdebug
{

/**
 * Initialize the hostname for the transforms module.
 */
void init_transforms();

/**
 * Write the post body data (called after body is complete).
 * @param txn The transaction.
 * @param data The BodyBuilder data.
 */
void writePostBody(TSHttpTxn txn, BodyBuilder *data);

/**
 * Main body transformation continuation handler.
 * @param contp The continuation.
 * @param event The event type.
 * @param edata Event data (unused).
 * @return Status code.
 */
int body_transform(TSCont contp, TSEvent event, void *edata);

} // namespace xdebug
