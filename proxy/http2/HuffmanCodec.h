/** @file

  A brief file description

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

#include <cstddef>
#include <cstdint>

void hpack_huffman_init();
void hpack_huffman_fin();
int64_t huffman_decode(char *dst_start, const uint8_t *src, uint32_t src_len);
uint8_t *huffman_encode_append(uint8_t *dst, uint32_t src, int n);
int64_t huffman_encode(uint8_t *dst_start, const uint8_t *src, uint32_t src_len);
