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

#pragma once

#include <string>

#include "ts/ts.h"

namespace ja4
{

/** Compute a JA4 fingerprint from an ATS ClientHello.
 *
 * @param[in] client_hello The ClientHello to fingerprint.
 * @return The JA4 fingerprint.
 */
std::string fingerprint(TSClientHello client_hello);

} // namespace ja4
