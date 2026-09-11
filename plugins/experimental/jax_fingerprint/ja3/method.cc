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

#include "ts/ts.h"

#include <plugin.h>
#include <context.h>
#include "fingerprint.h"
#include "method.h"

namespace ja3
{

void
on_client_hello(JAxContext *ctx, TSVConn vconn)
{
  TSClientHello ch = TSVConnClientHelloGet(vconn);

  if (!ch) {
    Dbg(dbg_ctl, "Could not get TSClientHello object.");
  } else {
    ctx->set_fingerprint(fingerprint(ch));
  }
}

struct Method method = {
  "JA3",
  Method::Type::CONNECTION_BASED,
  on_client_hello,
  nullptr,
};

} // namespace ja3
