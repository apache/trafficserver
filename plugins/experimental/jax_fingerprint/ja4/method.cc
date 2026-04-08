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
#include "method.h"
#include "ja4.h"
#include "datasource.h"
#include "tls_client_hello_summary.h"

namespace ja4_method
{

void on_client_hello(JAxContext *, TSVConn);

struct Method method = {
  "JA4",
  Method::Type::CONNECTION_BASED,
  on_client_hello,
  nullptr,
};

} // namespace ja4_method

void
ja4_method::on_client_hello(JAxContext *ctx, TSVConn vconn)
{
  char          fingerprint[JA4::FINGERPRINT_LENGTH];
  TSClientHello ch = TSVConnClientHelloGet(vconn);

  if (!ch) {
    Dbg(dbg_ctl, "Could not get TSClientHello object.");
  } else {
    TLSClientHelloSummary datasource{JA4::Datasource::Protocol::TLS, ch};

    JA4::make_JA4_fingerprint(fingerprint, datasource);

    ctx->set_fingerprint({fingerprint, JA4::FINGERPRINT_LENGTH});
  }
}
