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

constexpr int    FINGERPRINT_LENGTH   = 51;
constexpr size_t PART_A_POSITION      = 0;
constexpr size_t PART_B_POSITION      = 13;
constexpr size_t PART_C_POSITION      = 26;
constexpr size_t PART_D_POSITION      = 39;
constexpr size_t PART_A_LENGTH        = 12;
constexpr size_t PART_B_LENGTH        = 12;
constexpr size_t PART_C_LENGTH        = 12;
constexpr size_t PART_D_LENGTH        = 12;
constexpr char   DELIMITER            = '-';
constexpr size_t DELIMITER_1_POSITION = 12;
constexpr size_t DELIMITER_2_POSITION = 25;
constexpr size_t DELIMITER_3_POSITION = 38;

void generate_ja4h_fingerprint(char *out, Datasource &datasource);
