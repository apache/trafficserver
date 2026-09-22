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

#include "datasource.h"

namespace ja4
{

constexpr int    FINGERPRINT_LENGTH = 36;
constexpr size_t PART_A_POSITION    = 0;
constexpr size_t PART_B_POSITION    = 11;
constexpr size_t PART_C_POSITION    = 24;
constexpr size_t PART_A_LENGTH      = 10;
constexpr size_t PART_B_LENGTH      = 12;
constexpr size_t PART_C_LENGTH      = 12;
constexpr char   PORTION_DELIMITER{'_'};
constexpr size_t DELIMITER_1_POSITION = 10;
constexpr size_t DELIMITER_2_POSITION = 23;

/**
 * Calculate the JA4 fingerprint for the given TLS client hello.
 *
 * @param out Buffer of at least FINGERPRINT_LENGTH bytes. The fingerprint is
 * written to it without a terminating null.
 * @param datasource The TLS client hello. If there was no ALPN in the Client
 * Hello, the first ALPN value should be empty. Behavior when the number of
 * digits in the TLS version is greater than 2 is unspecified.
 * @return Returns a view of the JA4 fingerprint written to out.
 */
std::string_view generate_fingerprint(char *out, Datasource &datasource);

} // end namespace ja4
