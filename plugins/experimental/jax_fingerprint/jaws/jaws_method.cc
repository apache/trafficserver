/** @file

  JAWS v1 method for jax_fingerprint.

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

#include "context.h"
#include "ja3/ja3_summary.h"
#include "jaws.h"
#include "jaws_method.h"

namespace jaws_method
{

void on_client_hello(JAxContext *ctx, TSVConn vconn);
void on_vconn_close(TSVConn vconn);

struct Method method = {
  "JAWS", Method::Type::CONNECTION_BASED, on_client_hello, nullptr, on_vconn_close,
};

} // namespace jaws_method

void
jaws_method::on_client_hello(JAxContext *ctx, TSVConn vconn)
{
  if (auto const *summary = ja3::get_or_create_client_hello_summary(vconn); summary != nullptr) {
    ctx->set_fingerprint(ja3::jaws_v1::fingerprint(*summary));
  }
}

void
jaws_method::on_vconn_close(TSVConn vconn)
{
  ja3::clear_cached_client_hello_summary(vconn);
}
