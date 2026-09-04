/** @file

  JA4 TLS ClientHello fingerprint calculation.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements. See the NOTICE file distributed with this work for additional information regarding
  copyright ownership. Licensed under the Apache License, Version 2.0 (the "License"); you may not
  use this file except in compliance with the License. You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include "fingerprint.h"
#include "ja4.h"
#include "tls_client_hello_summary.h"

std::string
ja4::fingerprint(TSClientHello client_hello)
{
  char                  result[FINGERPRINT_LENGTH];
  TLSClientHelloSummary summary{Datasource::Protocol::TLS, client_hello};

  generate_fingerprint(result, summary);
  return {result, FINGERPRINT_LENGTH};
}
