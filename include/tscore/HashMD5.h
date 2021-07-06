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

#include "tscore/Hash.h"
#include <openssl/evp.h>

struct ATSHashMD5 : ATSHash {
  ATSHashMD5();
  void update(const void *data, size_t len) override;
  void final() override;
  const void *get() const override;
  size_t size() const override;
  void clear() override;
  ~ATSHashMD5() override;

private:
  EVP_MD_CTX *ctx;
  unsigned char md_value[EVP_MAX_MD_SIZE];
  unsigned int md_len = 0;
  bool finalized      = false;
};
