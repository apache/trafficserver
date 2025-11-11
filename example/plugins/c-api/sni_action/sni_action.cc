/** @file

  SSL SNI Action plugin.

  Demonstrates an SNI Action that is implemented by a plugin

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

#include <ts/ts.h>

#include <openssl/ssl.h>

#define PLUGIN_NAME "sni_action"
#define PCP         "[" PLUGIN_NAME "] "

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};
} // namespace

int
TSSNIDoAction(void *ih, SSL * /* ssl */)
{
  Dbg(dbg_ctl, "params: %s", static_cast<char *>(ih));

  // Randomly causes handshake failrue
  int random = rand();
  if (random % 5 == 0) {
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  } else {
    return SSL_TLSEXT_ERR_OK;
  }
}

TSReturnCode
TSSNINewInstance(int /* argc */, const char *argv[], void **ih)
{
  *ih = const_cast<char *>(argv[1]);
  return TS_SUCCESS;
}

TSReturnCode
TSSNIInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  return TS_SUCCESS;
}
