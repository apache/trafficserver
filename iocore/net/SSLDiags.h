/** @file

  Diags for TLS

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

#include "tscore/Diags.h"

class SSLNetVConnection;

// Log an SSL error.
#define SSLError(fmt, ...) SSLDiagnostic(MakeSourceLocation(), false, nullptr, fmt, ##__VA_ARGS__)
#define SSLErrorVC(vc, fmt, ...) SSLDiagnostic(MakeSourceLocation(), false, (vc), fmt, ##__VA_ARGS__)
// Log a SSL diagnostic using the "ssl" diagnostic tag.
#define SSLDebug(fmt, ...) SSLDiagnostic(MakeSourceLocation(), true, nullptr, fmt, ##__VA_ARGS__)
#define SSLVCDebug(vc, fmt, ...) SSLDiagnostic(MakeSourceLocation(), true, (vc), fmt, ##__VA_ARGS__)

void SSLDiagnostic(const SourceLocation &loc, bool debug, SSLNetVConnection *vc, const char *fmt, ...) TS_PRINTFLIKE(4, 5);

// Return a static string name for a SSL_ERROR constant.
const char *SSLErrorName(int ssl_error);

// Log a SSL network buffer.
void SSLDebugBufferPrint(const char *tag, const char *buffer, unsigned buflen, const char *message);
