/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <cstddef>
#include <cstdint>

class QUICVariableInt
{
public:
  static size_t size(const uint8_t *src);
  static size_t size(uint64_t src);
  static int encode(uint8_t *dst, size_t dst_len, size_t &len, uint64_t src);
  static int decode(uint64_t &dst, size_t &len, const uint8_t *src, size_t src_len = 8);
};

class QUICIntUtil
{
public:
  static uint64_t read_QUICVariableInt(const uint8_t *buf);
  static void write_QUICVariableInt(uint64_t data, uint8_t *buf, size_t *len);
  static uint64_t read_nbytes_as_uint(const uint8_t *buf, uint8_t n);
  static void write_uint_as_nbytes(uint64_t value, uint8_t n, uint8_t *buf, size_t *len);
};
