/** @file
 *

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

#include "utils.h"

constexpr int TRUNCATED_HASH_STRING_LENGTH = 6;

void
hash_stringify(char *out, const unsigned char *hash)
{
  for (int i = 0; i < TRUNCATED_HASH_STRING_LENGTH; ++i) {
    unsigned int h = hash[i] >> 4;
    unsigned int l = hash[i] & 0x0F;
    out[i * 2]     = h <= 9 ? ('0' + h) : ('a' + h - 10);
    out[i * 2 + 1] = l <= 9 ? ('0' + l) : ('a' + l - 10);
  }
}
