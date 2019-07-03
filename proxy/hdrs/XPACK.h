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

#include <cstdint>
#include "tscore/Arena.h"

const static int XPACK_ERROR_COMPRESSION_ERROR   = -1;
const static int XPACK_ERROR_SIZE_EXCEEDED_ERROR = -2;

// These primitives are shared with HPACK and QPACK.
int64_t xpack_encode_integer(uint8_t *buf_start, const uint8_t *buf_end, uint64_t value, uint8_t n);
int64_t xpack_decode_integer(uint64_t &dst, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n);
int64_t xpack_encode_string(uint8_t *buf_start, const uint8_t *buf_end, const char *value, uint64_t value_len, uint8_t n = 7);
int64_t xpack_decode_string(Arena &arena, char **str, uint64_t &str_length, const uint8_t *buf_start, const uint8_t *buf_end,
                            uint8_t n = 7);
