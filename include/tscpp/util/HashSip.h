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
#include <cstdint>

namespace ts
{
/*
  Siphash is a Hash Message Authentication Code and can take a key.

  If you don't care about MAC use the void constructor and it will use
  a zero key for you.
 */

struct Hash64Sip24 : public Hash64Functor {
  static constexpr size_t KEY_SIZE = 16;

  Hash64Sip24();

  Hash64Sip24(const unsigned char key[KEY_SIZE]);

  Hash64Sip24(std::uint64_t key0, std::uint64_t key1);

  Hash64Sip24 &update(std::string_view const &data) override;

  Hash64Sip24 & final() override;

  value_type get() const override;

  Hash64Sip24 &clear() override;

private:
  static constexpr size_t BLOCK_SIZE     = 8;
  unsigned char block_buffer[BLOCK_SIZE] = {0};
  std::uint8_t block_buffer_len          = 0;
  std::uint64_t k0                       = 0;
  std::uint64_t k1                       = 0;
  std::uint64_t v0                       = 0;
  std::uint64_t v1                       = 0;
  std::uint64_t v2                       = 0;
  std::uint64_t v3                       = 0;
  std::uint64_t hfinal                   = 0;
  std::size_t total_len                  = 0;
  bool finalized                         = false;
};

} // namespace ts
