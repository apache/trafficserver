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

/****************************************************************************

   HttpPluginHook.h

   Description:
       Http transaction debugging interfaces.


 ****************************************************************************/

#ifndef _HTTP_PLUGIN_HOOK_H
#define _HTTP_PLUGIN_HOOK_H

#include "ts.h"

// Define transaction session function type.
typedef void (*TxnSession_t)(TSHttpTxn txnp);
// Define the plugin hook function type.
typedef void (*PluginHook_t)(TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp, TSCont contp);

// Define two TxnSession functions to be called in init and kill_this function of Http state machine.
extern TxnSession_t txnBegin;
extern TxnSession_t txnEnd;

// Define two hook functions to handle before and after plugin execution.
extern PluginHook_t prePluginHook;
extern PluginHook_t postPluginHook;

// Initialize the two hook functions based on user provided library.
extern void initHttpTxnPluginHook();

#endif
