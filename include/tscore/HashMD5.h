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

#include "tscpp/util/Hash.h"
#include <openssl/evp.h>

namespace ts
{
struct HashMD5 : HashFunctor {
  HashMD5();

  HashMD5 &update(std::string_view const &data) override;

  HashMD5 & final() override;

  bool get(MemSpan dst) const override;

  size_t size() const override;

  HashMD5 &clear() override;

  ~HashMD5() override;

private:
  EVP_MD_CTX *ctx{nullptr};
  unsigned char md_value[EVP_MAX_MD_SIZE];
  unsigned int md_len{0};
  bool finalized{false};
};

} // namespace ts
