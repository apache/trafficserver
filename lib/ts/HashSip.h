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

#ifndef __HASH_SIP_H__
#define __HASH_SIP_H__

#include "ts/Hash.h"
#include <stdint.h>

/*
  Siphash is a Hash Message Authentication Code and can take a key.

  If you don't care about MAC use the void constructor and it will use
  a zero key for you.
 */

struct ATSHash64Sip24 : ATSHash64 {
  ATSHash64Sip24(void);
  ATSHash64Sip24(const unsigned char key[16]);
  ATSHash64Sip24(uint64_t key0, uint64_t key1);
  void update(const void *data, size_t len);
  void final(void);
  uint64_t get(void) const;
  void clear(void);

private:
  unsigned char block_buffer[8];
  uint8_t block_buffer_len;
  uint64_t k0, k1, v0, v1, v2, v3, hfinal;
  size_t total_len;
  bool finalized;
};

#endif
