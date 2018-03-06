/** @file

  SHA256 support class.

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

#ifndef _SHA256_h_
#define _SHA256_h_

#include "ts/ink_code.h"
#include "ts/ink_defs.h"
#include "ts/CryptoHash.h"
#include <openssl/sha.h>

class SHA256Context : public ats::CryptoContextBase
{
protected:
  SHA256_CTX _ctx;

public:
  SHA256Context() { SHA256_Init(&_ctx); }
  /// Update the hash with @a data of @a length bytes.
  virtual bool
  update(void const *data, int length)
  {
    return SHA256_Update(&_ctx, data, length);
  }
  /// Finalize and extract the @a hash.
  virtual bool
  finalize(CryptoHash &hash)
  {
    return SHA256_Final(hash.u8, &_ctx);
  }
};

#endif
